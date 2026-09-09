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

real atan(real y, real x)
{
    return std::atan2(y, x);
}

real length(vec3 v)
{
    return std::sqrt(dot(v, v));
}

bool finiteScalar(real v)
{
    return std::isfinite(v);
}

thread_local const SceneData* scene;
thread_local const RenderSettings* settings;

int sphereCount()
{
    return int(scene->spheres.size());
}

vec3 sphereCenter(int i)
{
    auto c = scene->spheres[i].centerRadius;

    return {c.x, c.y, c.z};
}

real sphereRadius(int i)
{
    return scene->spheres[i].centerRadius.w;
}

int sphereMaterial(int i)
{
    return scene->spheres[i].material.x;
}

int materialKind(int i)
{
    return scene->materials[i].kindTexture.x;
}

int materialTexture(int i)
{
    return scene->materials[i].kindTexture.y;
}

real materialParameter(int i)
{
    return scene->materials[i].parameters.x;
}

vec3 materialEmission(int i)
{
    auto p = scene->materials[i].parameters;
    return {p.y, p.z, p.w};
}

int blackHole()
{
    for (int i = 0; i < sphereCount(); ++i)
        if (materialKind(sphereMaterial(i)) == Schwarzschild)
            return i;

    return -1;
}

int planet()
{
    for (int i = 0; i < sphereCount(); ++i)
        if (materialKind(sphereMaterial(i)) == Earth)
            return i;

    return -1;
}

bool diskEnabled()
{
    return scene->disk.enabled;
}

vec3 diskNormal()
{
    return normalized(scene->disk.normal);
}

real diskInner()
{
    return scene->disk.innerRadius;
}

real diskOuter()
{
    return scene->disk.outerRadius;
}

real diskPeakTemperature()
{
    return scene->disk.peakTemperature;
}

real diskScale()
{
    return scene->disk.emissionScale;
}

real atmosphereHeight()
{
    return scene->atmosphereHeight;
}

bool redshiftEnabled()
{
    return settings->redshift;
}

vec3 thermalTexel(int i)
{
    auto c = blackbodyTable()[std::size_t(i)];
    return {c.x, c.y, c.z};
}

int textureKind(int i)
{
    return scene->textures[i].kindChildren.x;
}

int textureChild(int i, bool odd)
{
    auto t = scene->textures[i].kindChildren;

    return odd ? t.y : t.z;
}

vec3 textureColor(int i)
{
    auto c = scene->textures[i].color;

    return {c.x, c.y, c.z};
}

vec3 imageValue(int i, real u, real v)
{
    auto t = scene->textures[i].image;
    int x = clamp(int(u * t.y), 0, t.y - 1), y = clamp(int((1 - v) * t.z - 0.001), 0, t.z - 1);
    auto c = scene->texels[t.x + x + y * t.y];

    return {c.x, c.y, c.z};
}

real maximumStep()
{
    return settings->maxStep;
}

int integrationMode()
{
    return int(settings->integrationMode);
}

real relativeTolerance()
{
    return settings->relativeTolerance;
}

real absoluteTolerance()
{
    return settings->absoluteTolerance;
}

int maximumAttempts()
{
    return settings->maxIntegrationAttempts;
}

#define INOUT(T) T&
#define OUT(T) T&
#include "../assets/shaders/TraceCore.inl"
#undef INOUT
#undef OUT
} // namespace reference

RayResult traceCpu(
    const SceneData& scene, const RenderSettings& settings, Vec3 origin, Vec3 direction, std::uint32_t seed)
{
    reference::scene = &scene;
    reference::settings = &settings;
    reference::TraceState s;
    reference::initTrace(s, origin, direction, seed);

    while (s.status == 0)
        reference::advanceTrace(s);
    return {s.radiance, s.p, s.v, s.status, s.attempts};
}

std::vector<Float4> renderCpu(const SceneData& scene,
                              const RenderSettings& settings,
                              const CameraData& camera,
                              std::uint64_t* failures)
{
    scene.validate();
    settings.validate();
    auto basis = camera.basis(double(settings.width) / settings.height);
    std::vector<Float4> pixels(std::size_t(settings.width) * settings.height);
    std::atomic<int> row{0};
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
                        reference::hashBits(std::uint32_t(y * settings.width + x) ^
                                            reference::hashBits(std::uint32_t(sample) + settings.seed));
                    double u = (x + reference::randomValue(rng)) / settings.width,
                           v = (y + reference::randomValue(rng)) / settings.height;
                    auto result = traceCpu(
                        scene, settings, basis[0], basis[1] + u * basis[2] + v * basis[3] - basis[0], rng);
                    color += result.color;

                    if (result.status == 2)
                        ++bad;
                }

                color = color / settings.samples;
                pixels[std::size_t(y) * settings.width + x] = {
                    float(color.x), float(color.y), float(color.z), float(settings.samples)};
            }
    };
    std::vector<std::thread> workers;
    unsigned count = std::min(unsigned(settings.height), std::max(1u, std::thread::hardware_concurrency()));

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
    auto require = [](bool ok)
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

    scene.materials[0] = {{DiffuseLight, 0, 0, 0}, {0, 5778, 1, 0.6f}};
    auto source = reference::makeHit(0, {0, 0, 8}, {0, 0, 1}, 0);
    auto centerLight = reference::surfaceRadiance(source, {0, 0, 1}, 1);
    auto limbLight = reference::surfaceRadiance(source, {1, 0, 0}, 1);
    require(std::abs(limbLight.y / centerLight.y - (1.0 - double(0.6f))) < 1e-6);
    require(toneMap(100) >= toneMap(10) && toneMap(10) > toneMap(1));
}
} // namespace rt
