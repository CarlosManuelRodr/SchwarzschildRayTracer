#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace rt
{
    struct Vec3
    {
        double x = 0, y = 0, z = 0;
        Vec3() = default;

        explicit Vec3(const double s) : x(s), y(s), z(s){}
        Vec3(const double a, const double b, const double c) : x(a), y(b), z(c) {}
    };

    inline Vec3 operator+(const Vec3 &a, const Vec3 &b)
    {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }

    inline Vec3 operator-(const Vec3 &a, const Vec3 &b)
    {
        return {a.x - b.x, a.y - b.y, a.z - b.z};
    }

    inline Vec3 operator-(const Vec3 &a)
    {
        return {-a.x, -a.y, -a.z};
    }

    inline Vec3 operator*(const Vec3 &a, const double b)
    {
        return {a.x * b, a.y * b, a.z * b};
    }

    inline Vec3 operator*(const double b, const Vec3 &a)
    {
        return a * b;
    }

    inline Vec3 operator*(const Vec3 &a, const Vec3 &b)
    {
        return {a.x * b.x, a.y * b.y, a.z * b.z};
    }

    inline Vec3 operator/(const Vec3 &a, const double b)
    {
        return a * (1 / b);
    }

    inline Vec3& operator+=(Vec3& a, const Vec3 &b)
    {
        a = a + b;

        return a;
    }

    inline Vec3& operator*=(Vec3& a, const Vec3 &b)
    {
        a = a * b;

        return a;
    }

    double dot(const Vec3 &a, const Vec3 &b);
    Vec3 cross(const Vec3 &a, const Vec3 &b);
    Vec3 normalized(const Vec3 &a);

    // Explicit 16-byte lanes match GLSL std430 without relying on vec3 packing.
    struct alignas(16) Float4
    {
        float x = 0, y = 0, z = 0, w = 0;
    };

    struct alignas(16) Int4
    {
        int x = 0, y = 0, z = 0, w = 0;
    };

    enum MaterialKind
    {
        Lambertian,
        Metal,
        Dielectric,
        DiffuseLight,
        Schwarzschild,
        Earth,
        Environment,
        Moon
    };

    enum TextureKind
    {
        Constant,
        Checker,
        Image
    };

    struct SphereData
    {
        Float4 centerRadius;
        Int4 material;
    };

    struct MaterialData
    {
        Int4 kindTexture;
        // x: fuzz / IOR / environment intensity; thermal light y/z/w: kelvin, scale, limb.
        Float4 parameters;
        Int4 layers{-1, -1, -1, -1}; // Night, clouds, tangent-space normal, specular mask.
    };

    struct TextureData
    {
        Int4 kindChildren;
        Float4 color;
        Int4 image; // offset, width, height
    };

    struct AccretionDisk
    {
        bool enabled = false;
        float innerRadius = 3.0f;
        float outerRadius = 5.2f;
        float peakTemperature = 6000.0f;
        float emissionScale = 0.35f;
        Vec3 normal{0, 1, 0};
    };

    struct SceneData
    {
        std::vector<SphereData> spheres;
        std::vector<MaterialData> materials;
        std::vector<TextureData> textures;
        std::vector<Float4> texels;             // decoded linear RGB, original image row order
        std::vector<std::uint32_t> imageTexels; // Packed RGBA8; image.w: 1 = sRGB, 2 = linear data.
        AccretionDisk disk;
        float atmosphereHeight = 0.045f;

        void validate() const;
    };

    struct RenderSettings
    {
        enum IntegrationMode
        {
            FixedRadius,
            FullScene,
            AdaptiveCutoff
        };

        IntegrationMode integrationMode = FixedRadius;
        int width = 1440;
        int height = 900;
        int samples = 30;
        std::uint32_t seed = 1;
        bool redshift = true;
        float exposure = 1.0f;

        enum ObserverType
        {
            Hovering,
            FreelyFalling
        };

        ObserverType observerType = FreelyFalling;
        Vec3 observerVelocity{0, 0, 0}; // View-local right/up/forward, in units of c.
        bool useObserverFrame = true;   // False only for legacy numerical reference fixtures.

        float relativeTolerance = 1e-4f;
        float absoluteTolerance = 1e-6f;
        float maxStep = 0.05f; // Near-hole step scale; grows smoothly with squared radius beyond r=3.
        int maxIntegrationAttempts = 16384;

        void validate() const;
    };

    struct CameraData
    {
        Vec3 position{4, 3, 10};
        Vec3 lookAt{2, 0, 0};
        Vec3 up{0, 1, 0};
        double verticalFov = 75;

        void moveLocal(const Vec3 &direction, double distance);
        void rotateView(double yaw, double pitch);

        void validate() const;
        [[nodiscard]] std::array<Vec3, 4> basis(double aspect) const;
    };

    SceneData defaultScene(const std::filesystem::path& assets);

    RenderSettings observerSettings(const SceneData& scene,
                                    const RenderSettings& settings,
                                    const CameraData& camera);
    float decodeSrgb(float value);

    unsigned char encodeSrgb(double value);

    double toneMap(double value, double exposure = 1.0);

    Vec3 displayColor(const std::vector<Float4>& linear, int width, int height, int x, int y, float exposure = 1.0f);

    const std::vector<Float4>& blackbodyTable();

    void savePng(const std::filesystem::path& path,
                 int width,
                 int height,
                 const std::vector<Float4>& linear,
                 float exposure = 1.0f);

    std::vector<unsigned char> displayRgba(const std::vector<Float4>& linear, int width, int height,
                                           float exposure = 1.0f);

    void saveRgbaPng(const std::filesystem::path& path, int width, int height,
                     const std::vector<unsigned char>& rgba);
}
