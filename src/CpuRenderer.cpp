#include "CpuRenderer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <stdexcept>

namespace rt
{
    namespace reference
    {
        using real = double;
        using vec3 = Vec3;
        // ReSharper disable once CppTypeAliasNeverUsed
        using uint = std::uint32_t;
        using std::abs;
        using std::asin;
        using std::clamp;
        using std::cos;
        using std::exp;
        using std::floor;
        using std::log;
        using std::max;
        using std::min;
        using std::pow;
        using std::sin;
        using std::sqrt;

        static real atan(const real y, const real x)
        {
            return std::atan2(y, x);
        }

        static real length(const vec3 &v)
        {
            return std::sqrt(dot(v, v));
        }

        static bool finiteScalar(const real v)
        {
            return std::isfinite(v);
        }

        thread_local const SceneData* scene;
        thread_local const RenderSettings* settings;

        static int sphereCount()
        {
            return static_cast<int>(scene->spheres.size());
        }

        static vec3 sphereCenter(const int i)
        {
            auto c = scene->spheres[i].centerRadius;

            return {c.x, c.y, c.z};
        }

        static real sphereRadius(const int i)
        {
            return scene->spheres[i].centerRadius.w;
        }

        static int sphereMaterial(const int i)
        {
            return scene->spheres[i].material.x;
        }

        static int materialKind(const int i)
        {
            return scene->materials[i].kindTexture.x;
        }

        static int materialTexture(const int i)
        {
            return scene->materials[i].kindTexture.y;
        }

        static int materialLayer(const int i, const int layer)
        {
            const auto t = scene->materials[i].layers;
            return layer == 0 ? t.x : layer == 1 ? t.y : layer == 2 ? t.z : t.w;
        }

        static real materialParameter(const int i)
        {
            return scene->materials[i].parameters.x;
        }

        static vec3 materialEmission(const int i)
        {
            auto p = scene->materials[i].parameters;
            return {p.y, p.z, p.w};
        }

        static int blackHole()
        {
            for (int i = 0; i < sphereCount(); ++i)
                if (materialKind(sphereMaterial(i)) == Schwarzschild)
                    return i;

            return -1;
        }

        static int planet()
        {
            for (int i = 0; i < sphereCount(); ++i)
                if (materialKind(sphereMaterial(i)) == Earth)
                    return i;

            return -1;
        }

        static bool diskEnabled()
        {
            return scene->disk.enabled;
        }

        static vec3 diskNormal()
        {
            return normalized(scene->disk.normal);
        }

        static real diskInner()
        {
            return scene->disk.innerRadius;
        }

        static real diskOuter()
        {
            return scene->disk.outerRadius;
        }

        static real diskPeakTemperature()
        {
            return scene->disk.peakTemperature;
        }

        static real diskScale()
        {
            return scene->disk.emissionScale;
        }

        static real atmosphereHeight()
        {
            return scene->atmosphereHeight;
        }

        static bool redshiftEnabled()
        {
            return settings->redshift;
        }

        static vec3 thermalTexel(const int i)
        {
            auto c = blackbodyTable()[static_cast<std::size_t>(i)];
            return {c.x, c.y, c.z};
        }

        static int textureKind(const int i)
        {
            return scene->textures[i].kindChildren.x;
        }

        static int textureChild(const int i, const bool odd)
        {
            const auto t = scene->textures[i].kindChildren;

            return odd ? t.y : t.z;
        }

        static vec3 textureColor(const int i)
        {
            auto c = scene->textures[i].color;

            return {c.x, c.y, c.z};
        }

        static vec3 imageTexel(const int i, int x, int y)
        {
            const auto t = scene->textures[i].image;
            x = (x % t.y + t.y) % t.y;
            y = clamp(y, 0, t.z - 1);
            if (t.w == 0)
            {
                auto c = scene->texels[t.x + x + y * t.y];
                return {c.x, c.y, c.z};
            }
            const auto packed = scene->imageTexels[t.x + x + y * t.y];
            const float r = static_cast<float>(packed & 255u) / 255.f;
            const float g = static_cast<float>((packed >> 8) & 255u) / 255.f;
            const float b = static_cast<float>((packed >> 16) & 255u) / 255.f;
            return t.w == 1 ? vec3(decodeSrgb(r), decodeSrgb(g), decodeSrgb(b)) : vec3(r, g, b);
        }

        static vec3 imageValue(const int i, const real u, const real v)
        {
            const auto t = scene->textures[i].image;
            if (t.w == 0)
                return imageTexel(i, clamp(static_cast<int>(u * t.y), 0, t.y - 1), clamp(static_cast<int>((1 - v) * t.z - 0.001), 0, t.z - 1));
            double x = u * t.y - 0.5, y = (1 - v) * t.z - 0.5;
            int ix = static_cast<int>(floor(x)), iy = static_cast<int>(floor(y));
            double fx = x - ix, fy = y - iy;
            return (1 - fy) * ((1 - fx) * imageTexel(i, ix, iy) + fx * imageTexel(i, ix + 1, iy)) +
                   fy * ((1 - fx) * imageTexel(i, ix, iy + 1) + fx * imageTexel(i, ix + 1, iy + 1));
        }

        static real maximumStep()
        {
            return settings->maxStep;
        }

        static int integrationMode()
        {
            return static_cast<int>(settings->integrationMode);
        }

        static vec3 observerVelocity()
        {
            return settings->observerVelocity;
        }

        static bool observerFrameEnabled()
        {
            return settings->useObserverFrame;
        }

        static bool hoveringObserver()
        {
            return settings->observerType == RenderSettings::Hovering;
        }

        static real relativeTolerance()
        {
            return settings->relativeTolerance;
        }

        static real absoluteTolerance()
        {
            return settings->absoluteTolerance;
        }

        static int maximumAttempts()
        {
            return settings->maxIntegrationAttempts;
        }

        thread_local bool picking = false;
        thread_local int pickedBody = -1;

        #define INOUT(T) T&
        #define OUT(T) T&
        #include "../assets/shaders/TraceCore.inl"
        #undef INOUT
        #undef OUT
    } // namespace reference

    RayResult traceCpu(const SceneData& scene, const RenderSettings& settings, const Vec3 &origin, const Vec3 &direction,
                       const std::uint32_t seed)
    {
        reference::scene = &scene;
        reference::settings = &settings;
        reference::TraceState s;
        reference::initTrace(s, origin, direction, seed);

        while (s.status == 0)
            reference::advanceTrace(s);
        return {s.radiance, s.p, s.v, s.status, s.attempts};
    }

    int pickBody(const SceneData& scene, const RenderSettings& settings, const CameraData& camera, const double u,
                 const double v)
    {
        const auto observer = observerSettings(scene, settings, camera);
        const auto basis = camera.basis(static_cast<double>(settings.width) / settings.height);
        reference::picking = true;
        reference::pickedBody = -1;
        const auto result = traceCpu(scene, observer, basis[0], basis[1] + u * basis[2] + v * basis[3] - basis[0]);
        reference::picking = false;
        int body = reference::pickedBody;
        if (body < 0 && result.status == 3)
            for (int i = 0; i < static_cast<int>(scene.spheres.size()); ++i)
                if (scene.materials[scene.spheres[i].material.x].kindTexture.x == Schwarzschild)
                    body = i;

        if (body >= 0 && scene.materials[scene.spheres[body].material.x].kindTexture.x == Environment)
            return -1;

        return body;
    }

    std::vector<Float4> renderCpu(const SceneData& scene, const RenderSettings& settings, const CameraData& camera,
                                  std::uint64_t* failures)
    {
        scene.validate();
        settings.validate();
        const auto basis = camera.basis(static_cast<double>(settings.width) / settings.height);
        const auto observer = observerSettings(scene, settings, camera);
        std::vector<Float4> pixels(static_cast<std::size_t>(settings.width) * settings.height);
        std::atomic row{0};
        std::atomic<std::uint64_t> bad{0};
        auto worker = [&]
        {
            for (int y; (y = row.fetch_add(1)) < settings.height;)
                for (int x = 0; x < settings.width; ++x)
                {
                    Vec3 color(0);

                    for (int sample = 0; sample < settings.samples; ++sample)
                    {
                        auto rng =
                            reference::hashBits(static_cast<std::uint32_t>(y * settings.width + x) ^
                                                reference::hashBits(static_cast<std::uint32_t>(sample) + settings.seed));
                        double u = (x + reference::randomValue(rng)) / settings.width,
                               v = (y + reference::randomValue(rng)) / settings.height;
                        const auto result = traceCpu(scene, observer, basis[0], basis[1] + u * basis[2] + v * basis[3] - basis[0], rng);
                        color += result.color;

                        if (result.status == 2)
                            ++bad;
                    }

                    color = color / settings.samples;
                    pixels[static_cast<std::size_t>(y) * settings.width + x] = {
                        static_cast<float>(color.x), static_cast<float>(color.y), static_cast<float>(color.z), static_cast<float>(settings.samples)};
                }
        };
        std::vector<std::thread> workers;
        const unsigned count = std::min(static_cast<unsigned>(settings.height), std::max(1u, std::thread::hardware_concurrency()));

        workers.reserve(count);
        for (unsigned i = 0; i < count; ++i)
            workers.emplace_back(worker);

        for (auto& t : workers)
            t.join();

        if (failures)
            *failures = bad.load();
        return pixels;
    }

    void runCoreTests()
    {
        auto require = [](const bool ok)
        {
            if (!ok)
                throw std::runtime_error("CPU core numerical check failed");
        };
        double t = 0;
        require(reference::sphereRoot({0, 0, 3}, {0, 0, -1}, {0, 0, 0}, 1, 0, 100, t) && std::abs(t - 2) < 1e-12);
        require(reference::sphereRoot({0, 0, 0}, {0, 0, 1}, {0, 0, 0}, 1, 0, 100, t) && std::abs(t - 1) < 1e-12);
        require(!reference::sphereRoot({0, 0, 3}, {0, 1, 0}, {0, 0, 0}, 1, 0, 100, t));
        require(reference::length(reference::safeUnit({0, 0, 0})) == 0);

        // Independent analytic check: the unstable photon orbit is at r=1.5
        // for horizon radius 1, unit tangential velocity, and h²=2.25.
        Vec3 orbitP{1.5, 0, 0}, orbitV{0, 1, 0};

        for (int i = 0; i < 1000; ++i)
            reference::rk4(orbitP, orbitV, 0.005, {0, 0, 0}, 2.25, orbitP, orbitV);
        require(std::abs(reference::length(orbitP) - 1.5) < 1e-6);
        auto angular = cross(orbitP, orbitV);
        require(std::abs(dot(angular, angular) - 2.25) < 1e-8);
        SceneData scene;
        RenderSettings settings;
        settings.useObserverFrame = false;
        scene.texels = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1, 1, 1, 0}};
        TextureData image;
        image.kindChildren.x = Image;
        image.image = {0, 2, 2, 0};
        scene.textures.push_back(image);
        TextureData checker;
        checker.kindChildren = {Checker, 2, 3, 0};
        scene.textures.push_back(checker);
        TextureData odd;
        odd.color = {1, 0, 0, 0};
        scene.textures.push_back(odd);
        TextureData even;
        even.color = {0, 1, 0, 0};
        scene.textures.push_back(even);
        reference::scene = &scene;
        reference::settings = &settings;
        require(reference::imageValue(0, 0, 1).x == 1 && reference::imageValue(0, 1, 1).y == 1);
        require(reference::imageValue(0, 0, 0).z == 1 && reference::imageValue(0, 1, 0).x == 1);
        require(reference::imageValue(0, -2, 2).x == 1);
        require(reference::textureValue(1, 0, 0, {-1, 1, 1}).x == 1 &&
                reference::textureValue(1, 0, 0, {1, 1, 1}).y == 1);
        require(encodeSrgb(decodeSrgb(0.5f)) == 128);
        // Packed color is decoded before filtering; data maps remain linear.
        TextureData packedImage;
        packedImage.kindChildren.x = Image;
        packedImage.image = {0, 2, 1, 1};
        scene.imageTexels = {0x00808080u, 0x00ffffffu};
        scene.textures.push_back(packedImage);
        require(std::abs(reference::imageValue(4, 0.25, 0.5).x - decodeSrgb(128.f / 255.f)) < 1e-7);
        require(reference::length(reference::imageValue(4, 0, 0.5) - reference::imageValue(4, 1, 0.5)) < 1e-12);
        scene.textures[4].image.w = 2;
        require(std::abs(reference::imageValue(4, 0.25, 0.5).x - 128.0 / 255) < 1e-7);

        std::uint32_t a = 1, b = 1;

        for (int i = 0; i < 100; ++i)
        {
            auto x = reference::randomValue(a);
            require(x >= 0 && x < 1 && x == reference::randomValue(b));
        }

        scene.materials = {{{Schwarzschild, 0, 0, 0}, {}}};
        scene.spheres = {{{0, 0, 0, 8}, {0, 0, 0, 0}}};
        scene.disk.enabled = true;
        require(std::abs(reference::lapseAt({4, 0, 0}) - std::sqrt(0.75)) < 1e-12);
        require(reference::diskHit({4, 2, 0}, {0, -1, 0}, 0, 100, t) && std::abs(t - 2) < 1e-12);
        require(!reference::diskHit({0, 2, 0}, {0, -1, 0}, 0, 100, t));
        require(!reference::diskHit({6, 2, 0}, {0, -1, 0}, 0, 100, t));
        require(reference::diskTemperature(3) == 0);
        require(std::abs(reference::diskTemperature(49.0 / 12.0) - scene.disk.peakTemperature) < 0.001);

        double approaching = reference::diskFrequencyShift({4, 0, 0}, {0, 0, -1}, 1);
        double receding = reference::diskFrequencyShift({4, 0, 0}, {0, 0, 1}, 1);
        require(approaching > 1 && receding < 1);
        auto blue = reference::blackbody(6000 * approaching);
        auto red = reference::blackbody(6000 * receding);
        require(blue.z / blue.x > red.z / red.x && blue.y > red.y);
        settings.redshift = false;
        require(reference::diskFrequencyShift({4, 0, 0}, {0, 0, -1}, 1) == 1);
        settings.redshift = true;

        scene.textures[0].kindChildren.x = Constant;
        scene.textures[0].color = {1, 1, 1, 0};
        scene.materials[0] = {{DiffuseLight, 0, 0, 0}, {0, 5778, 1, 0.6f}};
        auto source = reference::makeHit(0, {0, 0, 8}, {0, 0, 1}, 0);
        auto centerLight = reference::surfaceRadiance(source, {0, 0, 1}, 1);
        auto limbLight = reference::surfaceRadiance(source, {1, 0, 0}, 1);
        require(std::abs(limbLight.y / centerLight.y - (1.0 - static_cast<double>(0.6f))) < 1e-6);
        require(toneMap(100) >= toneMap(10) && toneMap(10) > toneMap(1));

        // An unlit hemisphere emits city lights; clouds attenuate them. Daylight
        // must not add city emission. Use fixed samples to isolate the layer effect.
        SceneData layered;
        TextureData white, city, cloud;
        white.color = {1, 1, 1, 0};
        city.color = {1, 0, 0, 0};
        cloud.color = {0, 0, 0, 0};
        layered.textures = {white, city, cloud};
        layered.materials = {{{Earth, 0, 0, 0}, {}, {1, 2, -1, -1}}, {{DiffuseLight, 0, 0, 0}, {}}};
        layered.spheres = {{{0, 0, 0, 1}, {0, 0, 0, 0}}, {{0, 0, 5, 0.5f}, {1, 0, 0, 0}}};
        auto cityLight = traceCpu(layered, settings, {0, 0, -3}, {0, 0, 1});
        require(cityLight.color.x > 0.4 && cityLight.color.x <= 0.6 && cityLight.color.y == 0);
        layered.textures[2].color = {1, 1, 1, 0};
        auto obscuredCity = traceCpu(layered, settings, {0, 0, -3}, {0, 0, 1});
        require(obscuredCity.color.x < cityLight.color.x * 0.2);
        auto day = traceCpu(layered, settings, {0, 0, 3}, {0, 0, -1});
        layered.materials[0].layers.x = -1;
        require(reference::length(day.color - traceCpu(layered, settings, {0, 0, 3}, {0, 0, -1}).color) < 1e-12);
        reference::scene = &scene;
        reference::settings = &settings;

        // Independent null constraint and angular momentum checks for boosted
        // horizon-crossing initial data; legacy tests above intentionally use the old frame.
        scene.materials[0] = {{Schwarzschild, 0, 0, 0}, {}};
        settings.useObserverFrame = true;
        settings.observerType = RenderSettings::FreelyFalling;
        settings.observerVelocity = {0.2, -0.1, 0.3};
        reference::TraceState observerRay;
        reference::initTrace(observerRay, {0.7, 0, 0}, {1, 0.3, 0.2}, 1);
        require(observerRay.status == 0 && observerRay.observerMode == 3);
        auto momentum = cross(observerRay.p, observerRay.v);
        double angularSquared = dot(momentum, momentum);
        double energySquared = observerRay.energy * observerRay.energy;
        for (int i = 0; i < 100; ++i)
        {
            double radius = reference::length(observerRay.p);
            require(std::abs(dot(observerRay.v, observerRay.v) - angularSquared / (radius * radius * radius) -
                             energySquared) < 1e-7);
            require(reference::length(cross(observerRay.p, observerRay.v) - momentum) < 1e-8);
            reference::rk4(
                observerRay.p, observerRay.v, 0.005, {0, 0, 0}, angularSquared, observerRay.p, observerRay.v);
        }
    }
}