#include "SceneData.h"
#include "SdlSupport.h"
#include "stb_image.h"
#include <fstream>
#include <limits>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace rt
{
    double dot(const Vec3 &a, const Vec3 &b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec3 cross(const Vec3 &a, const Vec3 &b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }

    Vec3 normalized(const Vec3 &a)
    {
        const double n = std::sqrt(dot(a, a));
        return n > 1e-20 ? a / n : Vec3(0);
    }

    void CameraData::validate() const
    {
        if (!std::isfinite(dot(position, position)) || !std::isfinite(dot(lookAt, lookAt)) ||
            !std::isfinite(dot(up, up)) || !std::isfinite(verticalFov) || verticalFov <= 0 ||
            verticalFov >= 179)
        {
            throw std::runtime_error("Invalid camera settings");
        }

        const Vec3 forward = normalized(lookAt - position);
        const Vec3 right = normalized(cross(forward, up));

        if (dot(forward, forward) == 0 || dot(right, right) == 0)
            throw std::runtime_error("Degenerate camera basis");
    }

    std::array<Vec3, 4> CameraData::basis(const double aspect) const
    {
        validate();

        if (!std::isfinite(aspect) || aspect <= 0)
            throw std::runtime_error("Invalid camera aspect ratio");

        const Vec3 w = normalized(position - lookAt);
        const Vec3 u = normalized(cross(up, w));
        const Vec3 v = cross(w, u);
        const double h = std::tan(verticalFov * 3.141592653589793 / 360.0);

        return {position, position - h * aspect * u - h * v - w, 2 * h * aspect * u, 2 * h * v};
    }

    void CameraData::moveLocal(const Vec3 &direction, const double distance)
    {
        const Vec3 forward = normalized(lookAt - position);
        const Vec3 right = normalized(cross(forward, up));
        const Vec3 local = distance * normalized(direction);
        const Vec3 displacement = local.x * right + local.y * normalized(up) + local.z * forward;

        position += displacement;
        lookAt += displacement;
    }

    void CameraData::rotateView(const double yaw, const double pitch)
    {
        const Vec3 offset = lookAt - position;
        const double distance = std::sqrt(dot(offset, offset));
        const Vec3 forward = normalized(offset);
        const Vec3 vertical = normalized(up);
        Vec3 horizontal = normalized(forward - dot(forward, vertical) * vertical);
        const Vec3 right = normalized(cross(horizontal, vertical));
        double elevation = std::asin(std::clamp(dot(forward, vertical), -1.0, 1.0));

        // Stop just short of the poles to preserve a stable, roll-free camera basis.
        constexpr double pitchLimit = 89.0 * 3.141592653589793 / 180.0;
        elevation = std::clamp(elevation + pitch, -pitchLimit, pitchLimit);
        horizontal = std::cos(yaw) * horizontal + std::sin(yaw) * right;
        lookAt = position + distance * (std::cos(elevation) * horizontal + std::sin(elevation) * vertical);
    }

    float decodeSrgb(const float v)
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
        if (observerType != Hovering && observerType != FreelyFalling)
            throw std::runtime_error("Invalid observer type");
        const double speedSquared = dot(observerVelocity, observerVelocity);
        if (!std::isfinite(speedSquared) || speedSquared > 0.999 * 0.999 + 1e-12)
            throw std::runtime_error("Observer speed must not exceed 0.999c");
        if (integrationMode < FixedRadius || integrationMode > AdaptiveCutoff)
            throw std::runtime_error("Invalid integration mode");
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || samples <= 0 || samples > 1000000 ||
            !(exposure > 0) || !std::isfinite(exposure) || !(relativeTolerance > 0) || !(absoluteTolerance > 0) ||
            !(maxStep > 0) || maxStep > 0.05f || !std::isfinite(relativeTolerance) ||
            !std::isfinite(absoluteTolerance) || maxIntegrationAttempts <= 0 || maxIntegrationAttempts > 16384 ||
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * static_cast<std::uint64_t>(samples) > 0xffffffffull)
            throw std::runtime_error(
                "Invalid render settings (dimensions 1..8192, samples 1..1000000, step <= 0.05)");
    }

    void SceneData::validate() const
    {
        if (spheres.empty() || materials.empty() || textures.empty())
            throw std::runtime_error("Empty scene data");
        auto finite = [](const Float4 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
        };

        int fields = 0;
        for (auto [centerRadius, material] : spheres)
        {
            if (!finite(centerRadius) || centerRadius.w <= 0 || material.x < 0 ||
                material.x >= static_cast<int>(materials.size()))
                throw std::runtime_error("Invalid sphere");

            if (materials[material.x].kindTexture.x == Schwarzschild)
                ++fields;
        }

        if (fields > 1)
            throw std::runtime_error("Only one Schwarzschild black hole is supported");

        for (auto m : materials)
        {
            if (m.kindTexture.x < 0 || m.kindTexture.x > Moon || m.kindTexture.y < 0 ||
                m.kindTexture.y >= static_cast<int>(textures.size()) || !finite(m.parameters) ||
                (m.kindTexture.x == Dielectric && m.parameters.x <= 0))
            {
                throw std::runtime_error("Invalid material");
            }

            for (const int layer : {m.layers.x, m.layers.y, m.layers.z, m.layers.w})
            {
                if (layer < -1 || layer >= static_cast<int>(textures.size()))
                {
                    throw std::runtime_error("Invalid material texture layer");
                }
            }

            if (m.kindTexture.x == DiffuseLight &&
                (m.parameters.y < 0 || m.parameters.y > 50000 || m.parameters.z < 0 || m.parameters.w < 0 ||
                 m.parameters.w > 1))
            {
                throw std::runtime_error(
                    "Invalid thermal source (temperature 0..50000 K, limb coefficient 0..1)");
            }
        }

        if (disk.enabled &&
            (fields != 1 || !std::isfinite(disk.innerRadius) || disk.innerRadius < 3 ||
             !std::isfinite(disk.outerRadius) || disk.outerRadius <= disk.innerRadius ||
             !std::isfinite(disk.peakTemperature) || disk.peakTemperature < 1000 ||
             disk.peakTemperature > 20000 || !std::isfinite(disk.emissionScale) || disk.emissionScale < 0 ||
             !std::isfinite(dot(disk.normal, disk.normal)) || dot(disk.normal, disk.normal) < 1e-12))
        {
            throw std::runtime_error("Invalid accretion disk (inner radius >= 3, temperature 1000..20000 K)");
        }

        if (!std::isfinite(atmosphereHeight) || atmosphereHeight <= 0 || atmosphereHeight > 0.2f)
            throw std::runtime_error("Atmosphere height must be in (0, 0.2]");

        std::vector<int> visiting(textures.size());
        std::function<void(int, int)> check = [&](const int i, const int depth)
        {
            if (i < 0 || i >= static_cast<int>(textures.size()) || depth >= 32 || visiting[i])
                throw std::runtime_error("Invalid or cyclic texture graph");

            auto [kindChildren, color, image] = textures[i];
            if (!finite(color) || kindChildren.x < 0 || kindChildren.x > Image)
                throw std::runtime_error("Invalid texture");

            if (kindChildren.x == Image &&
                (image.x < 0 || image.y <= 0 || image.z <= 0 || image.w < 0 || image.w > 2 ||
                 static_cast<std::uint64_t>(image.x) + static_cast<std::uint64_t>(image.y) * image.z >
                     (image.w == 0 ? texels.size() : imageTexels.size())))
            {
                throw std::runtime_error("Invalid image storage");
            }

            if (kindChildren.x == Checker)
            {
                visiting[i] = 1;
                check(kindChildren.y, depth + 1);
                check(kindChildren.z, depth + 1);
                visiting[i] = 0;
            }
        };

        for (int i = 0; i < static_cast<int>(textures.size()); ++i)
            check(i, 0);

        for (const auto t : texels)
            if (!finite(t))
                throw std::runtime_error("Nonfinite texel");
    }

    RenderSettings observerSettings(const SceneData& scene,
                                    const RenderSettings& settings,
                                    const CameraData& camera)
    {
        auto result = settings;
        const auto basis = camera.basis(1);
        const Vec3 right = normalized(basis[2]);
        const Vec3 up = normalized(basis[3]);
        const Vec3 forward = normalized(camera.lookAt - camera.position);
        const auto velocity = settings.observerVelocity;
        result.observerVelocity = velocity.x * right + velocity.y * up + velocity.z * forward;
        bool interior = false;

        for (const auto [centerRadius, material] : scene.spheres)
        {
            if (scene.materials[material.x].kindTexture.x != Schwarzschild)
                continue;

            Vec3 offset = camera.position - Vec3(centerRadius.x, centerRadius.y, centerRadius.z);
            interior = dot(offset, offset) <= 1;
        }

        if (!interior && settings.observerType == RenderSettings::Hovering && dot(velocity, velocity) == 0)
            result.useObserverFrame = false; // Exact compatibility with the original exterior camera.

        return result;
    }

    SceneData defaultScene(const std::filesystem::path& assets)
    {
        SceneData s;
        auto image = [&](const char* name, const bool linear = false)
        {
            const auto path = assets / "textures" / name;
            // Read with filesystem::path to support Unicode asset paths on Windows.
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            const auto length = file ? static_cast<std::streamoff>(file.tellg()) : static_cast<std::streamoff>(-1);

            if (length <= 0 || length > std::numeric_limits<int>::max())
                throw std::runtime_error("Cannot read texture: " + path.string());

            std::vector<stbi_uc> encoded(static_cast<std::size_t>(length));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(encoded.data()), length))
                throw std::runtime_error("Cannot read texture: " + path.string());

            int width = 0, height = 0, channels = 0;
            const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(encoded.data(), static_cast<int>(length), &width, &height, &channels, 4),
                stbi_image_free
                );

            if (!pixels)
                throw std::runtime_error("Cannot decode texture: " + path.string());

            TextureData t;
            t.kindChildren.x = Image;
            t.image = {static_cast<int>(s.imageTexels.size()), width, height, linear ? 2 : 1};
            for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i)
                s.imageTexels.push_back(static_cast<std::uint32_t>(pixels.get()[4 * i]) |
                                        (static_cast<std::uint32_t>(pixels.get()[4 * i + 1]) << 8) |
                                        (static_cast<std::uint32_t>(pixels.get()[4 * i + 2]) << 16));
            s.textures.push_back(t);

            return static_cast<int>(s.textures.size() - 1);
        };
        // Reserve once: preserve all supplied detail without a multi-gigabyte float copy.
        s.imageTexels.reserve(220000000);
        const int earth = image("8k_earth_daymap.jpg");
        const int night = image("8k_earth_nightmap.jpg");
        const int clouds = image("8k_earth_clouds.jpg", true);
        const int normal = image("8k_earth_normal_map.png", true);
        const int specular = image("8k_earth_specular_map.png", true);
        const int sun = image("8k_sun.jpg");
        const int moon = image("8k_moon.jpg");
        const int sky = image("starbackground.jpg");
        s.materials = {{{Earth, earth, 0, 0}, {0.12f, 0, 0, 0}, {night, clouds, normal, specular}},
                       {{DiffuseLight, sun, 0, 0}, {0, 5778, 24, 0.6f}},
                       {{Schwarzschild, sun, 0, 0}, {}},
                       {{Environment, sky, 0, 0}, {0.2f, 0, 0, 0}},
                       {{Moon, moon, 0, 0}, {}}};
        // Translate the previous scene by +1 on Z, including the sky and default camera.
        // The Moon's radius is physical; its separation is compressed for illustration.
        s.spheres = {{{7, 0, 0, 1}, {0, 0, 0, 0}},
                     {{8, 4, 2, 0.65f}, {1, 0, 0, 0}},
                     {{0, 0, 0, 5.5f}, {2, 0, 0, 0}},
                     {{0, 0, 1, 200}, {3, 0, 0, 0}},
                     {{9.4f, 0.6f, 0, 0.2727f}, {4, 0, 0, 0}}};
        s.disk.enabled = true;
        s.validate();

        return s;
    }

    double toneMap(const double value, const double exposure)
    {
        if (!std::isfinite(value))
            return 0;

        const double x = std::max(0.0, value * exposure);
        return std::clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    }

    Vec3 displayColor(const std::vector<Float4>& linear, const int width, const int height, const int x, const int y, const float exposure)
    {
        Vec3 bloom;

        // A small, energy-limited camera glare filter. It does not illuminate geometry.
        for (int j = -2; j <= 2; ++j)
        {
            for (int i = -2; i <= 2; ++i)
            {
                const double weights[] = {1, 4, 6, 4, 1};
                const int sx = std::clamp(x + 4 * i, 0, width - 1);
                const int sy = std::clamp(y + 4 * j, 0, height - 1);
                const auto sample = linear[static_cast<std::size_t>(sy) * width + sx];
                Vec3 hdr = exposure * Vec3(sample.x, sample.y, sample.z);
                double luminance = dot(hdr, {0.2126, 0.7152, 0.0722});
                const double excess = std::max(0.0, luminance - 1.0) / std::max(luminance, 1e-8);
                bloom += hdr * (excess * weights[i + 2] * weights[j + 2] / 256.0);
            }
        }

        const auto pixel = linear[static_cast<std::size_t>(y) * width + x];
        const Vec3 hdr = exposure * Vec3(pixel.x, pixel.y, pixel.z) + 0.08 * bloom;
        return {toneMap(hdr.x), toneMap(hdr.y), toneMap(hdr.z)};
    }

    void savePng(const std::filesystem::path& path,
                 const int width,
                 const int height,
                 const std::vector<Float4>& linear,
                 const float exposure)
    {
        if (width <= 0 || height <= 0 || linear.size() != static_cast<std::size_t>(width) * height)
            throw std::runtime_error("Wrong image size for PNG export");
        saveRgbaPng(path, width, height, displayRgba(linear, width, height, exposure));
    }

    std::vector<unsigned char> displayRgba(const std::vector<Float4>& linear, const int width, const int height, const float exposure)
    {
        if (width <= 0 || height <= 0 || linear.size() != static_cast<std::size_t>(width) * height)
            throw std::runtime_error("Wrong image size for display conversion");
        std::vector<unsigned char> rgba(linear.size() * 4);

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto c = displayColor(linear, width, height, x, y, exposure);
                const auto i = 4 * (static_cast<std::size_t>(height - 1 - y) * width + x);
                rgba[i] = encodeSrgb(c.x);
                rgba[i + 1] = encodeSrgb(c.y);
                rgba[i + 2] = encodeSrgb(c.z);
                rgba[i + 3] = 255;
            }

        return rgba;
    }
    void saveRgbaPng(const std::filesystem::path& path, const int width, const int height, const std::vector<unsigned char>& rgba)
    {
        if (width <= 0 || height <= 0 || rgba.size() != static_cast<std::size_t>(width) * height * 4)
            throw std::runtime_error("Wrong RGBA image size");

        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());

        const SdlSurface image(
            SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, const_cast<unsigned char*>(rgba.data()), width * 4),
            SDL_DestroySurface
            );

        checkSdl(static_cast<bool>(image), "Create PNG surface");
        if (!SDL_SavePNG(image.get(), path.u8string().c_str()))
            throw std::runtime_error("Cannot save PNG: " + path.string() + ": " + SDL_GetError());
    }
}
