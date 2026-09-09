#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rt
{
struct Vec3
{
    double x = 0, y = 0, z = 0;
    Vec3() = default;

    explicit Vec3(double s) : x(s), y(s), z(s)
    {
    }

    Vec3(double a, double b, double c) : x(a), y(b), z(c)
    {
    }
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator-(Vec3 a)
{
    return {-a.x, -a.y, -a.z};
}

inline Vec3 operator*(Vec3 a, double b)
{
    return {a.x * b, a.y * b, a.z * b};
}

inline Vec3 operator*(double b, Vec3 a)
{
    return a * b;
}

inline Vec3 operator*(Vec3 a, Vec3 b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

inline Vec3 operator/(Vec3 a, double b)
{
    return a * (1 / b);
}

inline Vec3& operator+=(Vec3& a, Vec3 b)
{
    a = a + b;

    return a;
}

inline Vec3& operator*=(Vec3& a, Vec3 b)
{
    a = a * b;

    return a;
}

double dot(Vec3 a, Vec3 b);
Vec3 cross(Vec3 a, Vec3 b);
Vec3 normalized(Vec3 a);

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
    Environment
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
    std::vector<Float4> texels; // decoded linear RGB, original image row order
    AccretionDisk disk;
    float atmosphereHeight = 0.045f;

    void validate() const;
};

struct RenderSettings
{
    int width = 800;
    int height = 600;
    int samples = 30;
    std::uint32_t seed = 1;
    bool redshift = true;
    float exposure = 1.0f;

    float relativeTolerance = 1e-4f;
    float absoluteTolerance = 1e-6f;
    float maxStep = 0.05f;
    int maxIntegrationAttempts = 16384;

    void validate() const;
};

struct CameraData
{
    Vec3 position{4, 3, 9};
    Vec3 lookAt{2, 0, -1};
    Vec3 up{0, 1, 0};
    double verticalFov = 75;

    std::array<Vec3, 4> basis(double aspect) const;
};

SceneData defaultScene(const std::filesystem::path& assets);
float decodeSrgb(float value);
unsigned char encodeSrgb(double value);
double toneMap(double value, double exposure = 1.0);
Vec3 displayColor(
    const std::vector<Float4>& linear, int width, int height, int x, int y, float exposure = 1.0f);
const std::vector<Float4>& blackbodyTable();
void savePng(const std::filesystem::path& path,
             int width,
             int height,
             const std::vector<Float4>& linear,
             float exposure = 1.0f);
} // namespace rt
