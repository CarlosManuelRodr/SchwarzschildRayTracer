#include <GL/glew.h>
#include "GpuRenderer.h"
#include "SdlSupport.h"
#include "SettingsPanel.h"
#include "Timeline.h"
#include <imgui.h>
#include <imgui_internal.h>
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

        void waitFor(GpuRenderer &gpu)
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

        double imageRmse(const std::vector<Float4> &a, const std::vector<Float4> &b)
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
        navigation.validate();
        navigation.rotateView(0, -200);
        navigation.validate();
        require(std::abs(distance(navigation.position, navigation.lookAt) - 2) < 1e-12,
                "Pitch clamping must preserve focus distance and a valid basis");

        RenderSettings settings;
        settings.observerType = RenderSettings::Hovering;
        CameraData velocityCamera;
        velocityCamera.position = {0, 0, 4};
        velocityCamera.lookAt = {0, 0, 3};
        auto velocityScene = fieldScene();
        require(!observerSettings(velocityScene, settings, velocityCamera).useObserverFrame,
                "Zero-speed hovering must select the legacy exterior camera");
        settings.observerVelocity = {0, 0, 0.3};
        velocityCamera.rotateView(3.141592653589793 / 2, 0);
        require(distance(observerSettings(velocityScene, settings, velocityCamera).observerVelocity,
                         {0.3, 0, 0}) < 1e-12,
                "Forward velocity must rotate with the view");
        settings.observerVelocity = {0, 0.2, 0};
        velocityCamera.rotateView(0, 0.5);
        auto expectedUp = 0.2 * normalized(velocityCamera.basis(1)[3]);
        require(distance(observerSettings(velocityScene, settings, velocityCamera).observerVelocity,
                         expectedUp) < 1e-12,
                "Up velocity must follow camera pitch rather than world up");
        settings.observerVelocity = Vec3(0);
        velocityCamera.position = {0, 0, 0.5};
        require(observerSettings(velocityScene, settings, velocityCamera).useObserverFrame,
                "Interior hovering must reach observer validation, not the legacy camera path");
        settings.useObserverFrame = false;
        require(settings.integrationMode == RenderSettings::FixedRadius, "Fixed radius must be the default");
        settings.integrationMode = RenderSettings::FullScene;
        CameraData pickCamera;
        pickCamera.position = {0, 0, 3};
        pickCamera.lookAt = {0, 0, 0};
        RenderSettings pickSettings;

        for (int kind : {int(Earth), int(Moon), int(DiffuseLight)})
        {
            auto body = surfaceScene(kind);
            require(pickBody(body, pickSettings, pickCamera, 0.5, 0.5) == 0,
                    "Picking must identify the visible body");
        }

        auto emptySky = surfaceScene(Environment);
        require(pickBody(emptySky, pickSettings, pickCamera, 0.5, 0.5) == -1,
                "The environment must not be selectable");
        auto pickHole = fieldScene();
        require(pickBody(pickHole, pickSettings, pickCamera, 0.5, 0.5) == 0,
                "Captured rays must select the black hole");
        pickHole.disk.enabled = true;
        pickCamera.position = {4, 2, 0};
        pickCamera.lookAt = {4, 0, 0};
        pickCamera.up = {0, 0, 1};
        require(pickBody(pickHole, pickSettings, pickCamera, 0.5, 0.5) == 0,
                "Clicking the accretion disk must select its black hole");
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
        auto observerScene = fieldScene();
        observerScene.textures[0].color = {1, 1, 1, 0};
        observerScene.materials.push_back({{Environment, 0, 0, 0}, {1, 0, 0, 0}});
        observerScene.spheres.push_back({{0, 0, 0, 20}, {1, 0, 0, 0}});
        RenderSettings observerSettings;
        observerSettings.observerType = RenderSettings::Hovering;

        for (double radius : {0.5, 1.0})
        {
            require(traceCpu(observerScene, observerSettings, {radius, 0, 0}, {1, 0, 0}).status == 3,
                    "Hovering must not silently switch to a falling observer inside the horizon");
        }

        observerSettings.observerType = RenderSettings::FreelyFalling;
        observerSettings.integrationMode = RenderSettings::FullScene;

        for (double radius : {0.25, 0.9, 0.9999, 1.0, 1.0001, 2.0, 8.0})
        {
            for (double speed : {0.0, 0.5, -0.5, 0.999})
            {
                observerSettings.observerVelocity = {speed, 0, 0};
                auto observed = traceCpu(observerScene, observerSettings, {radius, 0, 0}, {1, 0, 0});
                double gamma = 1 / std::sqrt(1 - speed * speed);
                double energy = gamma * (1 - speed) * (1 + 1 / std::sqrt(radius));
                double expected = std::pow(std::sqrt(0.95) / energy, 4);
                require(observed.status == 1 &&
                            std::abs(observed.color.x - expected) < 1e-6 * std::max(1.0, expected),
                        "Rain observer radial redshift must match analytic frequency on both sides of the "
                        "horizon");
            }
        }

        observerSettings.observerVelocity = {0, 0, 0};

        for (int i = 0; i <= 64; ++i)
        {
            double angle = i * 3.141592653589793 / 64;
            double energy = 1 + std::cos(angle) / std::sqrt(0.8);
            double angular = 0.8 * std::sin(angle);
            double impactSquared = angular * angular / (energy * energy);

            if (std::abs(impactSquared - 6.75) < 0.01)
                continue;

            int expectedStatus = energy > 0 && impactSquared < 6.75 ? 1 : 3;
            auto cone =
                traceCpu(observerScene, observerSettings, {0.8, 0, 0}, {std::cos(angle), std::sin(angle), 0});
            require(cone.status == expectedStatus,
                    "Interior escape cone must match the analytic photon-sphere impact parameter");
        }

        require(traceCpu(observerScene, observerSettings, {0.5, 0, 0}, {-1, 0, 0}).status == 3,
                "Negative Killing energy cannot connect to the exterior stationary environment");
        observerSettings.redshift = false;

        for (auto mode :
             {RenderSettings::FixedRadius, RenderSettings::FullScene, RenderSettings::AdaptiveCutoff})
        {
            observerSettings.integrationMode = mode;
            require(distance(traceCpu(observerScene, observerSettings, {0.5, 0, 0}, {1, 0, 0}).color,
                             {1, 1, 1}) < 1e-8,
                    "Interior camera must bypass the exterior cutoff automatically");
        }

        std::cout
            << "CPU tests passed: intersections, textures, materials, capture, translation, convergence, "
               "bounded integration.\n";
        return 0;
    }

    int runGpuTests(const std::filesystem::path &assets)
    {
        runCpuTests();
        GpuRenderer gpu(assets);
        RenderSettings settings;
        settings.useObserverFrame = false;
        settings.integrationMode = RenderSettings::FullScene;
        std::cout << "Testing GPU: " << gpu.device() << std::endl;
        auto fit = gpu.fitResolution(16000, 9000);
        require(fit[0] > 0 && fit[1] > 0 && fit[0] <= 8192 && fit[1] <= 8192 &&
                    std::abs(double(fit[0]) / fit[1] - 16.0 / 9.0) < 0.01,
                "Large-window resolution fitting failed");
        std::vector<std::array<Vec3, 2>> rays = {
            {Vec3(0, 0, 4), Vec3(0, 0, -1)},    {Vec3(0, 0, 4), Vec3(0, 0, 1)},
            {Vec3(0, 0, 0), Vec3(1, 0, 0)},     {Vec3(-7, 5.49, 0), Vec3(1, 0, 0)},
            {Vec3(-7, 2.8, 0), Vec3(1, 0, 0)},  {Vec3(-7, 1.8, 0), Vec3(1, 0, 0)},
            {Vec3(-7, 2.45, 0), Vec3(1, 0, 0)}, {Vec3(-7, 2.5, 0), Vec3(1, 0, 0)}};
        auto field = fieldScene();
        auto results = gpu.traceRays(field, settings, rays);
        auto backgroundScene = fieldScene();
        backgroundScene.textures[0].color = {1, 0, 0, 0};
        backgroundScene.materials.push_back({{Environment, 0, 0, 0}, {1, 0, 0, 0}});
        backgroundScene.spheres.push_back({{0, 0, 0, 200}, {1, 0, 0, 0}});
        std::vector<std::array<Vec3, 2>> boundaryRays;

        for (int i = 0; i < 256; ++i)
        {
            double angle = i * 6.283185307179586 / 256;
            Vec3 direction{std::cos(angle), 0, std::sin(angle)};
            boundaryRays.push_back({199.999999 * direction, direction});
            boundaryRays.push_back({200.0 * direction, direction});
        }

        for (auto mode :
             {RenderSettings::FixedRadius, RenderSettings::FullScene, RenderSettings::AdaptiveCutoff})
        {
            auto boundarySettings = settings;
            boundarySettings.integrationMode = mode;
            boundarySettings.redshift = false;
            auto boundaryResults = gpu.traceRays(backgroundScene, boundarySettings, boundaryRays);

            for (std::size_t i = 0; i < boundaryRays.size(); ++i)
            {
                require(boundaryResults[i].status == 1 &&
                            distance(boundaryResults[i].color, {1, 0, 0}) < 1e-6,
                        "GPU background crossing must never leak fallback sky through rounding gaps");
                auto cpuBoundary =
                    traceCpu(backgroundScene, boundarySettings, boundaryRays[i][0], boundaryRays[i][1]);
                require(cpuBoundary.status == 1 && distance(cpuBoundary.color, {1, 0, 0}) < 1e-6,
                        "CPU background crossing must include points on the surface");
            }
        }

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

        for (auto &ray : shiftedRays)
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
        require(distance(embeddedHit[0].color, traceCpu(embedded, settings, {0, 0, 4}, {0, 0, -1}).color) <
                    1e-5,
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
        auto center = displayColor(first, settings.width, settings.height, settings.width / 2,
                                   settings.height / 2, settings.exposure);
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
        require(imageRmse(resized, renderCpu(scene, settings, camera)) < 0.06,
                "Camera reset render mismatch");
        auto path = std::filesystem::temp_directory_path() /
                    ("schwarzschild-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
        savePng(path, 1, 2, {{1, 0, 0, 1}, {0, 0, 1, 1}});
        SdlSurface png(SDL_LoadPNG(path.u8string().c_str()), SDL_DestroySurface);
        require(bool(png), "PNG export unreadable");
        auto pixelEquals = [&](int x, int y, Uint8 r, Uint8 g, Uint8 b)
        {
            Uint8 actualR, actualG, actualB, actualA;

            return SDL_ReadSurfacePixel(png.get(), x, y, &actualR, &actualG, &actualB, &actualA) &&
                   actualR == r && actualG == g && actualB == b && actualA == 255;
        };
        std::filesystem::remove(path);
        auto bright = encodeSrgb(toneMap(1));
        require(pixelEquals(0, 0, 0, 0, bright) && pixelEquals(0, 1, bright, 0, 0),
                "PNG orientation/color wrong");

        // A resize must not replace the displayed image or change its export dimensions.
        settings.width = 16;
        settings.height = 12;
        gpu.reset(settings, camera);
        gpu.saveDisplayed(path);
        png.reset(SDL_LoadPNG(path.u8string().c_str()));
        require(png && png->w == 37 && png->h == 23,
                "Export must retain displayed dimensions while the next view is pending");
        auto displayedCorner = displayColor(resized, 37, 23, 0, 22, settings.exposure);
        require(pixelEquals(0, 0, encodeSrgb(displayedCorner.x), encodeSrgb(displayedCorner.y),
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
        catch (const std::exception &)
        {
            missing = true;
        }

        require(missing, "Missing shader must fail explicitly");
        missing = false;

        try
        {
            defaultScene(assets / "missing");
        }
        catch (const std::exception &)
        {
            missing = true;
        }

        require(missing, "Missing image must fail explicitly");

        auto movingScene = fieldScene();
        movingScene.textures[0].color = {1, 1, 1, 0};
        movingScene.materials.push_back({{Environment, 0, 0, 0}, {1, 0, 0, 0}});
        movingScene.spheres.push_back({{0, 0, 0, 20}, {1, 0, 0, 0}});
        RenderSettings movingSettings;
        movingSettings.observerType = RenderSettings::FreelyFalling;
        movingSettings.integrationMode = RenderSettings::FullScene;
        std::vector<std::array<Vec3, 2>> movingRays;

        for (double radius : {0.25, 0.9, 0.9999, 1.0, 1.0001, 2.0})
            for (Vec3 direction : {Vec3(1, 0, 0), Vec3(1, 0.2, 0.3), Vec3(-1, 0, 0)})
                movingRays.push_back({Vec3(radius, 0, 0), direction});
        for (Vec3 velocity : {Vec3(0), Vec3(0.2, -0.1, 0.3), Vec3(0.999, 0, 0)})
        {
            movingSettings.observerVelocity = velocity;
            auto movingResults = gpu.traceRays(movingScene, movingSettings, movingRays);

            for (std::size_t i = 0; i < movingRays.size(); ++i)
            {
                auto expected = traceCpu(movingScene, movingSettings, movingRays[i][0], movingRays[i][1]);
                require(movingResults[i].status == expected.status && expected.status != 2 &&
                            distance(movingResults[i].color, expected.color) <
                                0.005 * std::max(1.0, distance(expected.color, Vec3(0))),
                        "Boosted horizon-crossing GPU rays must agree with double precision");
            }
        }

        movingSettings.observerVelocity = {0.1, 0.05, 0};
        movingSettings.width = 256;
        movingSettings.height = 160;
        movingSettings.samples = 30;
        movingSettings.exposure = 8;
        CameraData insideCamera;
        insideCamera.position = {0, 0.8, 0};
        insideCamera.lookAt = {4, 0, 0};
        insideCamera.verticalFov = 120;
        gpu.uploadScene(scene);
        gpu.reset(movingSettings, insideCamera);
        waitFor(gpu);
        auto interior = gpu.readback();
        auto interiorCpu = renderCpu(scene, movingSettings, insideCamera);
        double interiorError = imageRmse(interior, interiorCpu);
        std::cout << "Interior observer CPU/GPU RMSE: " << interiorError << std::endl;
        double energySum = 0, maximumError = 0;
        std::size_t worstPixel = 0;

        for (std::size_t i = 0; i < interior.size(); ++i)
        {
            auto expected = interiorCpu[i];
            energySum += double(expected.x) * expected.x + double(expected.y) * expected.y +
                         double(expected.z) * expected.z;
            double error =
                distance({interior[i].x, interior[i].y, interior[i].z}, {expected.x, expected.y, expected.z});
            if (error > maximumError)
            {
                maximumError = error;
                worstPixel = i;
            }
        }

        std::cout << "Interior reference RMS: " << std::sqrt(energySum / (3 * interior.size()))
                  << "; max error: " << maximumError << "; pixel: " << worstPixel
                  << "; GPU/CPU red: " << interior[worstPixel].x << "/" << interiorCpu[worstPixel].x
                  << "; invalid: " << gpu.progress().failures << std::endl;
        savePng(path, movingSettings.width, movingSettings.height, interior, movingSettings.exposure);
        std::cout << "Interior validation image: " << path.string() << std::endl;
        require(gpu.progress().failures == 0 && interiorError < 0.06,
                "Interior image must remain finite and agree with CPU reference");
        require(std::any_of(interior.begin(), interior.end(),
                            [](Float4 pixel)
                            {
                                return pixel.x + pixel.y + pixel.z > 0.001f;
                            }),
                "Interior observer must see light from the exterior");
        movingSettings.width = 32;
        movingSettings.height = 24;
        movingSettings.samples = 4;
        movingSettings.observerVelocity = {0.2, -0.1, 0.3};

        for (auto mode :
             {RenderSettings::FixedRadius, RenderSettings::FullScene, RenderSettings::AdaptiveCutoff})
        {
            movingSettings.integrationMode = mode;
            gpu.reset(movingSettings, CameraData{});
            waitFor(gpu);
            require(gpu.progress().failures == 0 &&
                        imageRmse(gpu.readback(), renderCpu(scene, movingSettings, CameraData{})) < 0.06,
                    "Exterior velocity rendering must agree with CPU in every integration mode");
        }

        require(glGetError() == GL_NO_ERROR, "OpenGL error during validation");
        movingSettings.observerType = RenderSettings::Hovering;
        movingSettings.observerVelocity = Vec3(0);
        gpu.reset(movingSettings, CameraData{});
        waitFor(gpu);
        auto hoveringImage = gpu.readback();
        auto legacySettings = movingSettings;
        legacySettings.useObserverFrame = false;
        gpu.reset(legacySettings, CameraData{});
        waitFor(gpu);
        require(imageRmse(hoveringImage, gpu.readback()) == 0 &&
                    imageRmse(renderCpu(scene, movingSettings, CameraData{}),
                              renderCpu(scene, legacySettings, CameraData{})) == 0,
                "Zero-speed hovering must exactly preserve legacy rendering");
        movingSettings.observerVelocity = {0.2, 0.1, -0.3};
        gpu.reset(movingSettings, CameraData{});
        waitFor(gpu);
        require(imageRmse(gpu.readback(), renderCpu(scene, movingSettings, CameraData{})) < 0.06,
                "Moving hovering observer must agree on CPU/GPU");
        for (double radius : {0.5, 1.0})
        {
            CameraData invalidCamera;
            invalidCamera.position = {radius, 0, 0};
            invalidCamera.lookAt = {radius + 1, 0, 0};
            gpu.reset(movingSettings, invalidCamera);
            waitFor(gpu);
            auto invalidImage = gpu.readback();
            require(std::all_of(invalidImage.begin(), invalidImage.end(),
                                [](Float4 pixel)
                                {
                                    return pixel.x == 0 && pixel.y == 0 && pixel.z == 0;
                                }),
                    "GPU hovering must remain invalid inside the horizon instead of switching frames");
        }
        // Position updates preserve textures and pick against the displayed snapshot,
        // including while a replacement scene is still being rendered.
        auto editedScene = surfaceScene(DiffuseLight);
        CameraData editCamera;
        editCamera.position = {0, 0, 3};
        editCamera.lookAt = {0, 0, 0};
        RenderSettings editSettings;
        editSettings.width = 32;
        editSettings.height = 24;
        editSettings.samples = 1;
        gpu.uploadScene(editedScene);
        gpu.reset(editSettings, editCamera);
        waitFor(gpu);
        require(gpu.pickDisplayed(0.5, 0.5) == 0, "Displayed body picking failed");
        editedScene.spheres[0].centerRadius.x = 5;
        gpu.updateGeometry(editedScene);
        gpu.reset(editSettings, editCamera);
        require(gpu.pickDisplayed(0.5, 0.5) == 0, "Pending edit must pick the retained scene");
        waitFor(gpu);
        require(gpu.pickDisplayed(0.5, 0.5) == -1, "Completed edit must pick the new scene");
        require(imageRmse(gpu.readback(), renderCpu(editedScene, editSettings, editCamera)) < 1e-5,
                "Geometry-only upload must match the CPU reference");
        editedScene.spheres[0].centerRadius.x = 0;
        gpu.updateGeometry(editedScene);
        gpu.reset(editSettings, editCamera);
        waitFor(gpu);
        require(gpu.pickDisplayed(0.5, 0.5) == 0, "Reset body position must restore picking");

        auto lensedScene = editedScene;
        lensedScene.spheres[0].centerRadius = {3, 0, 0, 0.65f};
        lensedScene.materials.push_back({{Schwarzschild, 0, 0, 0}, {}});
        lensedScene.spheres.push_back({{0, 0, 0, 5.5f}, {1, 0, 0, 0}});
        auto lensedCamera = editCamera;
        lensedCamera.position = {0, 0, 6};
        auto lensedSettings = editSettings;
        lensedSettings.width = 128;
        lensedSettings.height = 96;
        lensedSettings.integrationMode = RenderSettings::FullScene;
        gpu.uploadScene(lensedScene);

        for (auto observer : {RenderSettings::Hovering, RenderSettings::FreelyFalling})
        {
            lensedSettings.observerType = observer;
            lensedSettings.observerVelocity =
                observer == RenderSettings::FreelyFalling ? Vec3(0.2, 0.1, 0) : Vec3(0);
            gpu.reset(lensedSettings, lensedCamera);
            waitFor(gpu);
            auto anchor = gpu.bodyAnchors()[0];
            require(anchor.z == 1 && gpu.pickDisplayed(anchor.x, anchor.y) == 0,
                    "Gizmo anchor must lie on the lensed body in either observer frame");
            auto retained = anchor;
            lensedCamera.position.y += 0.1;
            gpu.reset(lensedSettings, lensedCamera);
            require(distance(gpu.bodyAnchors()[0], retained) == 0,
                    "Pending views must retain the displayed gizmo anchor");
            waitFor(gpu);
        }

        gpu.uploadScene(editedScene);
        gpu.reset(editSettings, editCamera);
        waitFor(gpu);

        {
            auto image = gpu.displayImage();
            int width = 0, height = 0;
            auto rgba = gpu.readDisplayedRgba(width, height);
            require(image.width == width && image.height == height,
                    "Viewport must preserve displayed image dimensions");
            std::vector<unsigned char> texture(rgba.size());
            glBindTexture(GL_TEXTURE_2D, image.texture);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture.data());

            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    for (int c = 0; c < 4; ++c)
                        require(std::abs(int(rgba[(y * width + x) * 4 + c]) -
                                         int(texture[((height - 1 - y) * width + x) * 4 + c])) <= 2,
                                "Docked viewport must match saved PNG colors and orientation");
        }
        // Exercise a real ImGui frame and synthetic SDL handle drag in a hidden window.
        auto window = SDL_GL_GetCurrentWindow();
        checkSdl(SDL_SetWindowSize(window, 1200, 800), "Size editor test window");
        {
            SettingsPanel panel(window, editSettings);
            panel.selectBody(0);
            panel.setDisplayImage(gpu.displayImage());
            double slowStep = 0.0005;
            editSettings.width = 1200;
            editSettings.height = 800;

            for (int i = 0; i < 3; ++i)
            {
                panel.beginFrame();
                panel.draw(slowStep, gpu.progress(), editSettings, false, editCamera, editedScene);
                panel.render();
            }

            auto viewport = panel.viewport();
            require(viewport.x > 0 && viewport.y > 0 && viewport.width > 0 &&
                        panel.viewportArea().width > 1190,
                    "Default workspace must give the viewport the full available width");
            require(!ImGui::FindWindowByName("Render Settings") && !panel.timelineVisible(),
                    "Optional panels must be hidden in a fresh workspace");
            require(!ImGui::FindWindowByName("Viewport")->HasCloseButton,
                    "Viewport must not have a close button");
            float handleX = viewport.x + viewport.width / 2 + 40 * ImGui::GetStyle().FontScaleDpi;
            float handleY = viewport.y + viewport.height / 2;
            ImGui::GetIO().AddMousePosEvent(handleX, handleY);
            panel.beginFrame();
            panel.draw(slowStep, gpu.progress(), editSettings, false, editCamera, editedScene);
            panel.render();
            SDL_Event press{};
            press.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            press.button.button = SDL_BUTTON_LEFT;
            press.button.x = handleX;
            press.button.y = handleY;
            panel.processEvent(press);
            require(panel.manipulatingBody(), "X translation handle must capture the mouse");
            SDL_Event move{};
            move.type = SDL_EVENT_MOUSE_MOTION;
            move.motion.x = handleX + 30;
            move.motion.y = handleY;
            panel.processEvent(move);
            panel.beginFrame();
            auto actions = panel.draw(slowStep, gpu.progress(), editSettings, false, editCamera, editedScene);
            panel.render();
            require(actions.body == 0 && actions.bodyPosition.x > 0 && actions.bodyPosition.y == 0 &&
                        actions.bodyPosition.z == 0,
                    "X handle must translate only the selected body's X axis");
            press.type = SDL_EVENT_MOUSE_BUTTON_UP;
            panel.processEvent(press);
            require(!panel.manipulatingBody(), "Releasing the handle must restore navigation");
            Timeline timeline(editedScene, editCamera);
            auto timelineFrame = [&]
            {
                panel.beginFrame();
                panel.draw(slowStep, gpu.progress(), editSettings, false, editCamera, editedScene);
                timeline.draw(panel, editSettings);
                auto active = editSettings;
                timeline.update(editedScene, editCamera, gpu, editSettings, active, false);
                panel.render();
            };
            timelineFrame();
            panel.selectCamera();
            timelineFrame();
            timelineFrame();
            require(panel.isCameraSelected() && panel.selectedBodyIndex() == -1,
                    "Camera selection must remain distinct from deselection");
            panel.selectBody(-1); // The viewport's existing background-pick action.
            timelineFrame();
            timelineFrame(); // Regression: the previous timeline reselected the body here.
            require(panel.selectedBodyIndex() == -1 && !panel.manipulatingBody(),
                    "Timeline must preserve background deselection across frames");
            panel.selectBody(0);
            timelineFrame();
            SDL_Event escape{};
            escape.type = SDL_EVENT_KEY_DOWN;
            escape.key.key = SDLK_ESCAPE;
            panel.processEvent(escape);
            timelineFrame();
            timelineFrame();
            require(panel.selectedBodyIndex() == -1,
                    "Escape must clear selection through the animation controller");
            auto dock = ImGui::FindWindowByName("Viewport")->DockId;
            SDL_Event toggle{};
            toggle.type = SDL_EVENT_KEY_DOWN;
            toggle.key.key = SDLK_F1;
            panel.processEvent(toggle);
            timelineFrame();
            require(!panel.isVisible() && panel.viewportArea().width > 1190,
                    "F1 must show the scene across the full window");
            panel.processEvent(toggle);
            timelineFrame();
            timelineFrame();
            require(panel.isVisible() && ImGui::FindWindowByName("Viewport")->DockId == dock,
                    "F1 must restore the dock layout");
            require(ImGui::GetIO().IniFilename == nullptr,
                    "Hidden validation must not modify workspace preferences");
            auto ini = std::string(ImGui::SaveIniSettingsToMemory());
            require(ini.find("[Docking][Data]") != std::string::npos,
                    "Workspace must serialize docking layout");
            // Reproduce the native picker returning after an odd-sized docked viewport.
            auto oddSettings = editSettings;
            oddSettings.width = 65;
            oddSettings.height = 49;
            oddSettings.samples = 1;
            timeline.prepareExport(oddSettings);
            require(timeline.width == 64 && timeline.height == 48,
                    "MP4 defaults must round viewport dimensions to even pixels");
            timeline.editor.configure(2, 30);
            timeline.exportSpec(oddSettings).validateSettings();
            timeline.width = 65;
            bool invalid = false;

            try
            {
                timeline.exportSpec(oddSettings).validateSettings();
            }
            catch (const std::exception &)
            {
                invalid = true;
            }

            require(invalid, "Odd MP4 dimensions must be rejected before opening a destination dialog");
            timeline.width = 64;
            auto target =
                std::filesystem::temp_directory_path() /
                ("timeline-dialog-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mp4");
#ifdef _WIN32
            timeline.dialogPending = true;
            timeline.dialogResult = std::make_shared<Timeline::DialogResult>();
            timeline.dialogResult->path = target.u8string();
            timeline.dialogResult->ready = true;
            timelineFrame();
            require(timeline.mode() == AnimationMode::Export && !timeline.dialogPending &&
                        !timeline.reopenExport,
                    "A chosen destination must start export once without reopening the picker");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

            while (timeline.busy() && std::chrono::steady_clock::now() < deadline)
            {
                gpu.poll();
                timelineFrame();
                gpu.dispatch();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            require(!timeline.busy() && std::filesystem::exists(target) &&
                        std::filesystem::file_size(target) > 0,
                    "Timeline export must finalize an MP4 after the destination callback");
            require(!timeline.dialogPending && !timeline.reopenExport,
                    "Successful export must not request another destination");
            std::filesystem::remove(target);
#endif
        }

        std::cout << "GPU tests passed: trajectories, materials/textures, deterministic rendering, "
                     "cancellation/resize, presentation, PNG, missing assets.\n";
        return 0;
    }
} // namespace rt
