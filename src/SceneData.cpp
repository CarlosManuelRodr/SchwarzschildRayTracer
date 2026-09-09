#include "SceneData.h"
#include <SFML/Graphics/Image.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace rt
{
double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(Vec3 a)
{
    double n = std::sqrt(dot(a, a));

    return n > 1e-20 ? a / n : Vec3(0);
}

std::array<Vec3, 4> CameraData::basis(double aspect) const
{
    if (!std::isfinite(dot(position, position)) || !std::isfinite(dot(lookAt, lookAt)) ||
        !std::isfinite(dot(up, up)) || !std::isfinite(verticalFov) || verticalFov <= 0 ||
        verticalFov >= 179 || !std::isfinite(aspect) || aspect <= 0)
        throw std::runtime_error("Invalid camera settings");
    Vec3 w = normalized(position - lookAt), u = normalized(cross(up, w));

    if (dot(w, w) == 0 || dot(u, u) == 0)
        throw std::runtime_error("Degenerate camera basis");
    Vec3 v = cross(w, u);
    double h = std::tan(verticalFov * 3.141592653589793 / 360.0);

    return {position, position - h * aspect * u - h * v - w, 2 * h * aspect * u, 2 * h * v};
}

void CameraData::moveLocal(Vec3 direction, double distance)
{
    Vec3 forward = normalized(lookAt - position);
    Vec3 right = normalized(cross(forward, up));
    Vec3 local = distance * normalized(direction);
    Vec3 displacement = local.x * right + local.y * normalized(up) + local.z * forward;

    position += displacement;
    lookAt += displacement;
}

void CameraData::rotateView(double yaw, double pitch)
{
    Vec3 offset = lookAt - position;
    double distance = std::sqrt(dot(offset, offset));
    Vec3 forward = normalized(offset);
    Vec3 vertical = normalized(up);
    Vec3 horizontal = normalized(forward - dot(forward, vertical) * vertical);
    Vec3 right = normalized(cross(horizontal, vertical));
    double elevation = std::asin(std::clamp(dot(forward, vertical), -1.0, 1.0));

    // Stop just short of the poles to preserve a stable, roll-free camera basis.
    constexpr double pitchLimit = 89.0 * 3.141592653589793 / 180.0;
    elevation = std::clamp(elevation + pitch, -pitchLimit, pitchLimit);
    horizontal = std::cos(yaw) * horizontal + std::sin(yaw) * right;
    lookAt = position + distance * (std::cos(elevation) * horizontal + std::sin(elevation) * vertical);
}

float decodeSrgb(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

unsigned char encodeSrgb(double v)
{
    if (!std::isfinite(v))
        v = 0;
    v = std::clamp(v, 0.0, 1.0);
    v = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;

    return static_cast<unsigned char>(std::round(v * 255));
}

void RenderSettings::validate() const
{
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || samples <= 0 || samples > 1000000 ||
        !(exposure > 0) || !std::isfinite(exposure) || !(relativeTolerance > 0) || !(absoluteTolerance > 0) ||
        !(maxStep > 0) || maxStep > 0.05f || !std::isfinite(relativeTolerance) ||
        !std::isfinite(absoluteTolerance) || maxIntegrationAttempts <= 0 || maxIntegrationAttempts > 16384 ||
        std::uint64_t(width) * std::uint64_t(height) * std::uint64_t(samples) > 0xffffffffull)
        throw std::runtime_error(
            "Invalid render settings (dimensions 1..8192, samples 1..1000000, step <= 0.05)");
}

void SceneData::validate() const
{
    if (spheres.empty() || materials.empty() || textures.empty())
        throw std::runtime_error("Empty scene data");
    auto finite = [](Float4 v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
    };
    int fields = 0;

    for (auto s : spheres)
    {
        if (!finite(s.centerRadius) || s.centerRadius.w <= 0 || s.material.x < 0 ||
            s.material.x >= int(materials.size()))
            throw std::runtime_error("Invalid sphere");

        if (materials[s.material.x].kindTexture.x == Schwarzschild)
        {
            ++fields;

            if (s.centerRadius.w <= 1)
                throw std::runtime_error("Gravity region must exceed horizon radius 1");
        }
    }

    if (fields > 1)
        throw std::runtime_error("Only one Schwarzschild gravity region is supported");

    for (auto m : materials)
    {
        if (m.kindTexture.x < 0 || m.kindTexture.x > Environment || m.kindTexture.y < 0 ||
            m.kindTexture.y >= int(textures.size()) || !finite(m.parameters) ||
            (m.kindTexture.x == Dielectric && m.parameters.x <= 0))
            throw std::runtime_error("Invalid material");

        if (m.kindTexture.x == DiffuseLight &&
            (m.parameters.y < 0 || m.parameters.y > 50000 || m.parameters.z < 0 || m.parameters.w < 0 ||
             m.parameters.w > 1))
            throw std::runtime_error(
                "Invalid thermal source (temperature 0..50000 K, limb coefficient 0..1)");
    }

    if (disk.enabled &&
        (fields != 1 || !std::isfinite(disk.innerRadius) || disk.innerRadius < 3 ||
         !std::isfinite(disk.outerRadius) || disk.outerRadius <= disk.innerRadius ||
         !std::isfinite(disk.peakTemperature) || disk.peakTemperature < 1000 ||
         disk.peakTemperature > 20000 || !std::isfinite(disk.emissionScale) || disk.emissionScale < 0 ||
         !std::isfinite(dot(disk.normal, disk.normal)) || dot(disk.normal, disk.normal) < 1e-12))
        throw std::runtime_error("Invalid accretion disk (inner radius >= 3, temperature 1000..20000 K)");

    if (!std::isfinite(atmosphereHeight) || atmosphereHeight <= 0 || atmosphereHeight > 0.2f)
        throw std::runtime_error("Atmosphere height must be in (0, 0.2]");
    std::vector<int> visiting(textures.size());
    std::function<void(int, int)> check = [&](int i, int depth)
    {
        if (i < 0 || i >= int(textures.size()) || depth >= 32 || visiting[i])
            throw std::runtime_error("Invalid or cyclic texture graph");
        auto t = textures[i];

        if (!finite(t.color) || t.kindChildren.x < 0 || t.kindChildren.x > Image)
            throw std::runtime_error("Invalid texture");

        if (t.kindChildren.x == Image &&
            (t.image.x < 0 || t.image.y <= 0 || t.image.z <= 0 ||
             std::uint64_t(t.image.x) + std::uint64_t(t.image.y) * t.image.z > texels.size()))
            throw std::runtime_error("Invalid image storage");

        if (t.kindChildren.x == Checker)
        {
            visiting[i] = 1;
            check(t.kindChildren.y, depth + 1);
            check(t.kindChildren.z, depth + 1);
            visiting[i] = 0;
        }
    };

    for (int i = 0; i < int(textures.size()); ++i)
        check(i, 0);

    for (auto t : texels)
        if (!finite(t))
            throw std::runtime_error("Nonfinite texel");
}

SceneData defaultScene(const std::filesystem::path& assets)
{
    SceneData s;
    auto image = [&](const char* name)
    {
        sf::Image img;
        auto path = assets / "textures" / name;

        if (!img.loadFromFile(path.string()))
            throw std::runtime_error("Cannot load texture: " + path.string());
        auto size = img.getSize();
        TextureData t;
        t.kindChildren.x = Image;
        t.image = {int(s.texels.size()), int(size.x), int(size.y), 0};
        auto pixels = img.getPixelsPtr();

        for (std::size_t i = 0; i < std::size_t(size.x) * size.y; ++i)
            s.texels.push_back({decodeSrgb(pixels[4 * i] / 255.f),
                                decodeSrgb(pixels[4 * i + 1] / 255.f),
                                decodeSrgb(pixels[4 * i + 2] / 255.f),
                                0});
        s.textures.push_back(t);

        return int(s.textures.size() - 1);
    };
    int earth = image("earthmap.jpg"), sky = image("starbackground.jpg");
    TextureData light;
    light.color = {2, 2, 2, 0};
    s.textures.push_back(light);
    s.materials = {{{Earth, earth, 0, 0}, {0.12f, 0, 0, 0}},
                   {{DiffuseLight, 2, 0, 0}, {0, 5778, 24, 0.6f}},
                   {{Schwarzschild, 2, 0, 0}, {}},
                   {{Environment, sky, 0, 0}, {0.2f, 0, 0, 0}}};
    s.spheres = {{{7, 0, -1, 1}, {0, 0, 0, 0}},
                 {{8, 4, 1, 0.65f}, {1, 0, 0, 0}},
                 {{0, 0, -1, 5.5f}, {2, 0, 0, 0}},
                 {{0, 0, 0, 200}, {3, 0, 0, 0}}};
    s.disk.enabled = true;
    s.validate();

    return s;
}

double toneMap(double value, double exposure)
{
    if (!std::isfinite(value))
        return 0;

    double x = std::max(0.0, value * exposure);
    return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

Vec3 displayColor(const std::vector<Float4>& linear, int width, int height, int x, int y, float exposure)
{
    const double weights[] = {1, 4, 6, 4, 1};
    Vec3 bloom;

    // A small, energy-limited camera glare filter. It does not illuminate geometry.
    for (int j = -2; j <= 2; ++j)
    {
        for (int i = -2; i <= 2; ++i)
        {
            int sx = std::clamp(x + 4 * i, 0, width - 1);
            int sy = std::clamp(y + 4 * j, 0, height - 1);
            auto sample = linear[std::size_t(sy) * width + sx];
            Vec3 hdr = exposure * Vec3(sample.x, sample.y, sample.z);
            double luminance = dot(hdr, {0.2126, 0.7152, 0.0722});
            double excess = std::max(0.0, luminance - 1.0) / std::max(luminance, 1e-8);
            bloom += hdr * (excess * weights[i + 2] * weights[j + 2] / 256.0);
        }
    }

    auto pixel = linear[std::size_t(y) * width + x];
    Vec3 hdr = exposure * Vec3(pixel.x, pixel.y, pixel.z) + 0.08 * bloom;
    return {toneMap(hdr.x), toneMap(hdr.y), toneMap(hdr.z)};
}

void savePng(const std::filesystem::path& path,
             int width,
             int height,
             const std::vector<Float4>& linear,
             float exposure)
{
    if (linear.size() != std::size_t(width) * height)
        throw std::runtime_error("Wrong image size for PNG export");
    std::vector<sf::Uint8> rgba(linear.size() * 4);

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto c = displayColor(linear, width, height, x, y, exposure);
            auto i = 4 * (std::size_t(height - 1 - y) * width + x);
            rgba[i] = encodeSrgb(c.x);
            rgba[i + 1] = encodeSrgb(c.y);
            rgba[i + 2] = encodeSrgb(c.z);
            rgba[i + 3] = 255;
        }

    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    sf::Image image;
    image.create(unsigned(width), unsigned(height), rgba.data());

    if (!image.saveToFile(path.string()))
        throw std::runtime_error("Cannot save PNG: " + path.string());
}
} // namespace rt
