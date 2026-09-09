#include <GL/glew.h>
#include "GpuRenderer.h"
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

namespace rt
{
namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

double distance(Vec3 a, Vec3 b)
{
    auto d = a - b;

    return std::sqrt(dot(d, d));
}

SceneData fieldScene(Vec3 center = Vec3(0))
{
    SceneData s;
    s.textures.push_back({});
    s.materials.push_back({{Schwarzschild, 0, 0, 0}, {}});
    s.spheres.push_back({{float(center.x), float(center.y), float(center.z), 5.5f}, {}});

    return s;
}

SceneData surfaceScene(int kind)
{
    SceneData s;
    TextureData t;
    t.color = {0.25f, 0.5f, 0.75f, 0};
    s.textures.push_back(t);
    s.materials.push_back({{kind, 0, 0, 0}, {kind == Dielectric ? 1.5f : 0.f, 0, 0, 0}});
    s.spheres.push_back({{0, 0, 0, 1}, {}});

    return s;
}

void waitFor(GpuRenderer& gpu)
{
    auto start = std::chrono::steady_clock::now();

    while (!gpu.progress().finished)
    {
        gpu.poll();
        gpu.dispatch();
        std::this_thread::yield();

        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(120))
            throw std::runtime_error("GPU validation timed out");
    }
}

double imageRmse(const std::vector<Float4>& a, const std::vector<Float4>& b)
{
    require(a.size() == b.size(), "Image dimension mismatch");
    double sum = 0;

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        require(std::isfinite(a[i].x) && std::isfinite(a[i].y) && std::isfinite(a[i].z),
                "Nonfinite GPU output");
        double x = a[i].x - b[i].x, y = a[i].y - b[i].y, z = a[i].z - b[i].z;
        sum += x * x + y * y + z * z;
    }

    return std::sqrt(sum / (3 * a.size()));
}
} // namespace

void runCoreTests();

int runCpuTests()
{
    runCoreTests();

    CameraData navigation;
    navigation.position = {0, 0, 0};
    navigation.lookAt = {0, 0, -2};
    navigation.rotateView(3.141592653589793 / 2, 0);
    require(distance(navigation.lookAt, {2, 0, 0}) < 1e-12,
            "Mouse yaw must turn right and preserve focus distance");
    navigation.moveLocal({0, 0, 1}, 1);
    require(distance(navigation.position, {1, 0, 0}) < 1e-12,
            "Forward movement must follow the rotated view");
    navigation.moveLocal({1, 0, 0}, 1);
    require(distance(navigation.position, {1, 0, 1}) < 1e-12, "Strafing must use camera right");
    navigation.rotateView(0, 100);
    auto beforeMove = navigation.position;
    navigation.moveLocal({0, 0, 1}, 1);
    require(navigation.position.y > beforeMove.y + 0.99, "Forward movement must follow camera pitch");
    navigation.basis(4.0 / 3.0);
    navigation.rotateView(0, -200);
    navigation.basis(4.0 / 3.0);
    require(std::abs(distance(navigation.position, navigation.lookAt) - 2) < 1e-12,
            "Pitch clamping must preserve focus distance and a valid basis");

    RenderSettings settings;
    require(settings.integrationMode == RenderSettings::FixedRadius, "Fixed radius must be the default");
    settings.integrationMode = RenderSettings::FullScene;
    auto field = fieldScene();
    field.validate();
    require(traceCpu(field, settings, {0, 0, 4}, {0, 0, -1}).status == 3, "Radial ray must be captured");
    require(traceCpu(field, settings, {0, 0, 0}, {1, 0, 0}).status == 3,
            "Ray inside horizon must be captured");
    auto outward = traceCpu(field, settings, {0, 0, 4}, {0, 0, 1});
    require(outward.status == 1 && distance(normalized(outward.direction), {0, 0, 1}) < 1e-12,
            "Radial escape must remain straight");
    Vec3 origin{-7, 2.8, 0}, direction{1, 0, 0}, translation{3, -2, 5};
    auto original = traceCpu(field, settings, origin, direction);
    auto changedRadius = field;
    changedRadius.spheres[0].centerRadius.w = 50;
    auto withoutCutoff = traceCpu(changedRadius, settings, origin, direction);
    require(original.status == withoutCutoff.status &&
                distance(original.direction, withoutCutoff.direction) == 0 &&
                distance(original.position, withoutCutoff.position) == 0,
            "Legacy gravity radius must not affect trajectories");
    auto distant = traceCpu(field, settings, {8, 0, 0}, {0, 0, 1});
    require(distant.status == 1 && distant.direction.x < -0.001,
            "Rays entirely outside the old gravity region must still bend");
    auto fixed = settings;
    fixed.integrationMode = RenderSettings::FixedRadius;
    require(distance(traceCpu(field, fixed, {8, 0, 0}, {0, 0, 1}).direction, {0, 0, 1}) == 0,
            "Default mode must retain straight rays outside the fixed region");
    auto adaptive = settings;
    adaptive.integrationMode = RenderSettings::AdaptiveCutoff;
    auto radialAdaptive = traceCpu(field, adaptive, {4, 0, 0}, {1, 0, 0});
    auto radialFull = traceCpu(field, settings, {4, 0, 0}, {1, 0, 0});
    require(radialAdaptive.status == radialFull.status && radialAdaptive.attempts < radialFull.attempts &&
                distance(radialAdaptive.direction, radialFull.direction) < 1e-12,
            "Adaptive mode must skip negligible bending without changing radial escape");
    require(traceCpu(field, adaptive, {4, 0, 0}, {-1, 0, 0}).status == 3,
            "Adaptive cutoff must not bypass horizon capture");
    auto tightAdaptive = adaptive;
    tightAdaptive.relativeTolerance = 1e-9f;
    tightAdaptive.absoluteTolerance = 1e-11f;
    auto weak = traceCpu(field, adaptive, {4, 0, 0}, {1, 0.001, 0});
    auto tightWeak = traceCpu(field, tightAdaptive, {4, 0, 0}, {1, 0.001, 0});
    require(weak.status == tightWeak.status && weak.attempts < tightWeak.attempts &&
                distance(weak.direction, tightWeak.direction) < adaptive.relativeTolerance,
            "Tighter cutoff tolerance must resolve previously skipped weak bending");
    auto justInside = traceCpu(field, settings, {5.4999, 0, 0}, {-0.2, 0, 1});
    auto justOutside = traceCpu(field, settings, {5.5001, 0, 0}, {-0.2, 0, 1});
    require(justInside.status == justOutside.status &&
                distance(justInside.direction, justOutside.direction) < 0.001,
            "Crossing the legacy radius must not introduce a lensing discontinuity");
    auto translated = traceCpu(fieldScene(translation), settings, origin + translation, direction);
    require(original.status == translated.status &&
                distance(original.direction, translated.direction) < 1e-8 &&
                distance(original.position + translation, translated.position) < 1e-8,
            "Gravity must be translation invariant");
    auto fine = settings;
    fine.maxStep = 0.005f;
    fine.relativeTolerance = 1e-8f;
    fine.absoluteTolerance = 1e-10f;
    auto medium = settings;
    medium.maxStep = 0.025f;
    medium.relativeTolerance = 1e-6f;
    medium.absoluteTolerance = 1e-8f;
    auto ref = traceCpu(field, fine, origin, direction), mid = traceCpu(field, medium, origin, direction);
    require(distance(mid.direction, ref.direction) <= distance(original.direction, ref.direction) + 1e-8,
            "Integrator must converge with tighter steps/tolerances");
    auto limited = settings;
    limited.maxIntegrationAttempts = 1;
    require(traceCpu(field, limited, {0, 0, 4}, {0, 0, -1}).status == 2,
            "Integration budget exhaustion must be diagnosed");
    auto emitter = surfaceScene(DiffuseLight);
    auto light = traceCpu(emitter, settings, {0, 0, 3}, {0, 0, -1});
    require(distance(light.color, {0.25, 0.5, 0.75}) < 1e-12, "Emitted radiance must not be normalized");

    for (int kind : {Lambertian, Metal, Dielectric, DiffuseLight})
    {
        auto s = surfaceScene(kind);
        s.validate();
        auto result = traceCpu(s, settings, {0, 0, 3}, {0, 0, -1});
        require(result.status == 1 && std::isfinite(dot(result.color, result.color)),
                "Material trace failed");
    }

    field.materials.push_back({{DiffuseLight, 0, 0, 0}, {}});
    field.textures[0].color = {2, 1, 0.5f, 0};
    field.spheres.push_back({{0, 0, 3, 0.25f}, {1, 0, 0, 0}});
    double staticShiftPower = std::pow((1.0 - 1.0 / 3.25) / (1.0 - 1.0 / 4.0), 2);
    require(distance(traceCpu(field, settings, {0, 0, 4}, {0, 0, -1}).color,
                     staticShiftPower * Vec3(2, 1, 0.5)) < 1e-8,
            "Missed surface inside gravity region");

    auto earth = surfaceScene(Earth);
    earth.materials.push_back({{DiffuseLight, 0, 0, 0}, {0, 5778, 12, 0.6f}});
    earth.spheres.push_back({{0, 3, 4, 0.5f}, {1, 0, 0, 0}});
    auto lit = traceCpu(earth, settings, {0, 0, 3}, {0, 0, -1});
    require(dot(lit.color, lit.color) > 1e-5, "Earth must receive direct sunlight");
    auto night = traceCpu(earth, settings, {0, 0, -3}, {0, 0, 1});
    require(dot(night.color, night.color) < 0.01 * dot(lit.color, lit.color),
            "Planet must shadow its night side");
    earth.spheres.push_back({{0, 1.5f, 2.5f, 0.8f}, {0, 0, 0, 0}});
    auto shadow = traceCpu(earth, settings, {0, 0, 3}, {0, 0, -1});
    require(dot(shadow.color, shadow.color) < 0.1 * dot(lit.color, lit.color),
            "Area-light shadows must block sunlight");
    std::cout << "CPU tests passed: intersections, textures, materials, capture, translation, convergence, "
                 "bounded integration.\n";
    return 0;
}

int runGpuTests(const std::filesystem::path& assets)
{
    runCpuTests();
    GpuRenderer gpu(assets);
    RenderSettings settings;
    settings.integrationMode = RenderSettings::FullScene;
    std::cout << "Testing GPU: " << gpu.device() << std::endl;
    auto fit = gpu.fitResolution(16000, 9000);
    require(fit[0] > 0 && fit[1] > 0 && fit[0] <= 8192 && fit[1] <= 8192 &&
                std::abs(double(fit[0]) / fit[1] - 16.0 / 9.0) < 0.01,
            "Large-window resolution fitting failed");
    std::vector<std::array<Vec3, 2>> rays = {{Vec3(0, 0, 4), Vec3(0, 0, -1)},
                                             {Vec3(0, 0, 4), Vec3(0, 0, 1)},
                                             {Vec3(0, 0, 0), Vec3(1, 0, 0)},
                                             {Vec3(-7, 5.49, 0), Vec3(1, 0, 0)},
                                             {Vec3(-7, 2.8, 0), Vec3(1, 0, 0)},
                                             {Vec3(-7, 1.8, 0), Vec3(1, 0, 0)},
                                             {Vec3(-7, 2.45, 0), Vec3(1, 0, 0)},
                                             {Vec3(-7, 2.5, 0), Vec3(1, 0, 0)}};
    auto field = fieldScene();
    auto results = gpu.traceRays(field, settings, rays);
    for (auto mode : {RenderSettings::FixedRadius, RenderSettings::AdaptiveCutoff})
    {
        auto modeSettings = settings;
        modeSettings.integrationMode = mode;
        auto modeResults = gpu.traceRays(field, modeSettings, rays);
        for (std::size_t i = 0; i < rays.size(); ++i)
        {
            auto reference = traceCpu(field, modeSettings, rays[i][0], rays[i][1]);
            require(modeResults[i].status == reference.status &&
                        distance(modeResults[i].direction, reference.direction) < 0.005,
                    "Integration modes must agree between CPU and GPU");
            if (mode == RenderSettings::AdaptiveCutoff)
                require(modeResults[i].status == results[i].status &&
                            distance(modeResults[i].direction, results[i].direction) < 0.005,
                        "Adaptive cutoff must preserve full-scene capture and deflection");
        }
    }
    auto differentRadius = field;
    differentRadius.spheres[0].centerRadius.w = 50;
    auto radiusResults = gpu.traceRays(differentRadius, settings, rays);
    for (std::size_t i = 0; i < results.size(); ++i)
        require(results[i].status == radiusResults[i].status &&
                    distance(results[i].direction, radiusResults[i].direction) == 0,
                "GPU trajectory must be independent of the legacy gravity radius");

    for (std::size_t i = 0; i < rays.size(); ++i)
    {
        auto cpu = traceCpu(field, settings, rays[i][0], rays[i][1]);
        std::cout << "Ray " << i << ": status " << results[i].status << " vs " << cpu.status
                  << ", direction error " << distance(results[i].direction, cpu.direction) << std::endl;
        require(results[i].status == cpu.status, "GPU/CPU capture classification mismatch");
        require(distance(results[i].position, cpu.position) < 0.005 &&
                    distance(results[i].direction, cpu.direction) < 0.005,
                "GPU trajectory differs from double reference");
    }

    Vec3 translation{3, -2, 5};
    auto shiftedRays = rays;

    for (auto& ray : shiftedRays)
        ray[0] += translation;
    auto shifted = gpu.traceRays(fieldScene(translation), settings, shiftedRays);

    for (std::size_t i = 0; i < rays.size(); ++i)
        require(shifted[i].status == results[i].status &&
                    distance(shifted[i].position, results[i].position + translation) < 0.005,
                "GPU translation invariance failed");
    auto limited = settings;
    limited.maxIntegrationAttempts = 1;
    auto exhausted = gpu.traceRays(field, limited, {{Vec3(0, 0, 4), Vec3(0, 0, -1)}});
    require(exhausted[0].status == 2 && exhausted[0].attempts == 1 && gpu.progress().failures == 1,
            "GPU attempt limit/diagnostic failed");
    auto embedded = field;
    embedded.materials.push_back({{DiffuseLight, 0, 0, 0}, {}});
    embedded.textures[0].color = {2, 1, 0.5f, 0};
    embedded.spheres.push_back({{0, 0, 3, 0.25f}, {1, 0, 0, 0}});
    auto embeddedHit = gpu.traceRays(embedded, settings, {{Vec3(0, 0, 4), Vec3(0, 0, -1)}});
    require(distance(embeddedHit[0].color, traceCpu(embedded, settings, {0, 0, 4}, {0, 0, -1}).color) < 1e-5,
            "GPU missed object within gravity region");

    auto diskScene = fieldScene();
    diskScene.disk.enabled = true;
    std::vector<std::array<Vec3, 2>> diskRays = {{Vec3(4, 2, 1), Vec3(0, -1, 0)},
                                                 {Vec3(-4, 2, 1), Vec3(0, -1, 0)},
                                                 {Vec3(0, 2, 0), Vec3(0, -1, 0)},
                                                 {Vec3(4, -2, 0), Vec3(0, 1, 0)}};
    auto diskResults = gpu.traceRays(diskScene, settings, diskRays);
    for (std::size_t i = 0; i < diskRays.size(); ++i)
    {
        auto reference = traceCpu(diskScene, settings, diskRays[i][0], diskRays[i][1]);
        require(diskResults[i].status == reference.status &&
                    distance(diskResults[i].color, reference.color) < 0.002,
                "GPU disk/redshift mismatch");
    }

    for (int kind : {Lambertian, Metal, Dielectric, DiffuseLight})
    {
        auto scene = surfaceScene(kind);
        auto result = gpu.traceRays(scene, settings, {{Vec3(0, 0, 3), Vec3(0, 0, -1)}});
        auto cpu = traceCpu(scene, settings, {0, 0, 3}, {0, 0, -1});
        require(distance(result[0].color, cpu.color) < 1e-4, "GPU material mismatch");
    }

    auto textured = surfaceScene(DiffuseLight);
    textured.textures[0].kindChildren = {Checker, 1, 2, 0};
    TextureData odd, even;
    odd.color = {1, 0, 0, 0};
    even.color = {0, 1, 0, 0};
    textured.textures.push_back(odd);
    textured.textures.push_back(even);
    std::vector<std::array<Vec3, 2>> textureRays = {{Vec3(3, 3, 3), Vec3(-1, -1, -1)},
                                                    {Vec3(-3, 3, 3), Vec3(1, -1, -1)}};
    auto checker = gpu.traceRays(textured, settings, textureRays);
    require(distance(checker[0].color, {0, 1, 0}) < 1e-6 && distance(checker[1].color, {1, 0, 0}) < 1e-6,
            "GPU checker texture failed");
    textured.textures[0].kindChildren.x = Image;
    textured.textures[0].image = {0, 2, 2, 0};
    textured.texels = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 1, 1, 0}};
    textureRays = {{Vec3(0, 3, 0), Vec3(0, -1, 0)},
                   {Vec3(0, -3, 0), Vec3(0, 1, 0)},
                   {Vec3(3, 0, 0), Vec3(-1, 0, 0)},
                   {Vec3(0, 0, 3), Vec3(0, 0, -1)}};
    auto texturedResults = gpu.traceRays(textured, settings, textureRays);

    for (std::size_t i = 0; i < textureRays.size(); ++i)
        require(distance(texturedResults[i].color,
                         traceCpu(textured, settings, textureRays[i][0], textureRays[i][1]).color) < 1e-6,
                "GPU image UV boundary mismatch");
    auto scene = defaultScene(assets);
    settings.width = 64;
    settings.height = 48;
    settings.samples = 8;
    gpu.uploadScene(scene);
    gpu.reset(settings, CameraData{});
    waitFor(gpu);
    auto first = gpu.readback(), cpu = renderCpu(scene, settings, CameraData{});
    double rmse = imageRmse(first, cpu);
    std::cout << "Default scene linear RGB RMSE: " << rmse << std::endl;
    require(rmse < 0.035, "GPU image differs excessively from CPU reference");
    require(gpu.progress().failures == 0, "GPU invalid rays in default scene");
    gpu.present(1, 1);
    unsigned char screen[4]{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, screen);
    auto center = displayColor(
        first, settings.width, settings.height, settings.width / 2, settings.height / 2, settings.exposure);
    require(std::abs(int(screen[0]) - int(encodeSrgb(center.x))) <= 1 &&
                std::abs(int(screen[1]) - int(encodeSrgb(center.y))) <= 1 &&
                std::abs(int(screen[2]) - int(encodeSrgb(center.z))) <= 1,
            "Display shader gamma/presentation mismatch");
    gpu.reset(settings, CameraData{});
    auto firstPassDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool sawIncompletePass = false;
    while (!gpu.progress().firstPassComplete)
    {
        gpu.dispatch();
        auto partial = gpu.readback();
        for (auto pixel : partial)
            require(pixel.w <= 1, "Extra samples must wait for the complete first pass");

        if (!gpu.progress().firstPassComplete)
        {
            sawIncompletePass = true;
            gpu.present(1, 1);
            unsigned char retained[4]{};
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, retained);
            require(std::equal(std::begin(screen), std::end(screen), std::begin(retained)),
                    "Incomplete first pass must preserve the displayed image");
        }
        require(std::chrono::steady_clock::now() < firstPassDeadline, "First pass did not complete");
    }
    require(sawIncompletePass, "First-pass test must exercise unfinished trajectories");
    for (auto pixel : gpu.readback())
        require(pixel.w == 1, "Complete first pass must cover every pixel exactly once");
    waitFor(gpu);
    require(imageRmse(first, gpu.readback()) == 0, "GPU fixed seed must be repeatable after reset");
    auto camera = CameraData{};
    camera.position.x += 0.5;
    camera.lookAt.x += 0.5;
    settings.width = 37;
    settings.height = 23;
    settings.samples = 2;
    gpu.reset(settings, CameraData{});
    gpu.dispatch(); // Cancel an in-flight view safely.
    gpu.reset(settings, camera);
    waitFor(gpu);
    auto resized = gpu.readback();
    require(resized.size() == 851, "Resize failed");

    for (auto p : resized)
        require(p.w == 2, "Accumulation retained stale samples");
    require(imageRmse(resized, renderCpu(scene, settings, camera)) < 0.06, "Camera reset render mismatch");
    auto path = std::filesystem::temp_directory_path() /
                ("schwarzschild-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    savePng(path, 1, 2, {{1, 0, 0, 1}, {0, 0, 1, 1}});
    sf::Image png;
    require(png.loadFromFile(path.string()), "PNG export unreadable");
    std::filesystem::remove(path);
    auto bright = encodeSrgb(toneMap(1));
    require(png.getPixel(0, 0) == sf::Color(0, 0, bright) && png.getPixel(0, 1) == sf::Color(bright, 0, 0),
            "PNG orientation/color wrong");

    // A resize must not replace the displayed image or change its export dimensions.
    settings.width = 16;
    settings.height = 12;
    gpu.reset(settings, camera);
    gpu.saveDisplayed(path);
    require(png.loadFromFile(path.string()) && png.getSize() == sf::Vector2u(37, 23),
            "Export must retain displayed dimensions while the next view is pending");
    auto displayedCorner = displayColor(resized, 37, 23, 0, 22, settings.exposure);
    require(png.getPixel(0, 0) == sf::Color(encodeSrgb(displayedCorner.x),
                                            encodeSrgb(displayedCorner.y),
                                            encodeSrgb(displayedCorner.z)),
            "Displayed export must preserve the previous image across resize");
    std::filesystem::remove(path);
    bool missing = false;

    for (auto mode : {RenderSettings::FixedRadius, RenderSettings::AdaptiveCutoff})
    {
        settings.integrationMode = mode;
        gpu.reset(settings, camera);
        waitFor(gpu);
        auto modeImage = gpu.readback();
        require(gpu.progress().failures == 0 &&
                    imageRmse(modeImage, renderCpu(scene, settings, camera)) < 0.06,
                "Mode render must be finite and agree with the CPU reference");
    }

    try
    {
        GpuRenderer invalid(assets / "missing");
    }
    catch (const std::exception&)
    {
        missing = true;
    }

    require(missing, "Missing shader must fail explicitly");
    missing = false;

    try
    {
        defaultScene(assets / "missing");
    }
    catch (const std::exception&)
    {
        missing = true;
    }

    require(missing, "Missing image must fail explicitly");
    require(glGetError() == GL_NO_ERROR, "OpenGL error during validation");
    std::cout << "GPU tests passed: trajectories, materials/textures, deterministic rendering, "
                 "cancellation/resize, presentation, PNG, missing assets.\n";
    return 0;
}
} // namespace rt
