/**
 * @file
 * @brief Scene storage, camera conventions, render settings, and image conversion.
 */
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace rt
{
    /**
     * @brief Double-precision Cartesian vector, also used for linear RGB colors.
     *
     * Positions and lengths use horizon-radius units; the event horizon has radius 1.
     */
    struct Vec3
    {
        double x = 0, y = 0, z = 0;
        Vec3() = default;

        explicit Vec3(const double s) : x(s), y(s), z(s) {}

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

    inline Vec3 &operator+=(Vec3 &a, const Vec3 &b)
    {
        a = a + b;

        return a;
    }

    inline Vec3 &operator*=(Vec3 &a, const Vec3 &b)
    {
        a = a * b;

        return a;
    }

    /**
     * @brief Return the Euclidean scalar product.
     */
    double dot(const Vec3 &a, const Vec3 &b);

    /**
     * @brief Return the right-handed vector product.
     */
    Vec3 cross(const Vec3 &a, const Vec3 &b);

    /**
     * @brief Return a unit vector, or zero when its length is at most 1e-20.
     */
    Vec3 normalized(const Vec3 &a);

    /**
     * @brief Four floating-point lanes matching a GLSL vec4 in std430 storage.
     *
     * Explicit padding avoids the different packing rules for three-component vectors.
     */
    struct alignas(16) Float4
    {
        float x = 0, y = 0, z = 0, w = 0;
    };

    /**
     * @brief Four integer lanes matching a GLSL ivec4 in std430 storage.
     */
    struct alignas(16) Int4
    {
        int x = 0, y = 0, z = 0, w = 0;
    };

    /**
     * @brief Material dispatch indices shared with TraceCore.inl.
     *
     * Keep these numeric values synchronized with the shader material accessors.
     */
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

    /**
     * @brief Texture dispatch indices: constant RGB, spatial checker, or image.
     */
    enum TextureKind
    {
        Constant,
        Checker,
        Image
    };

    /**
     * @brief GPU sphere record. centerRadius.xyz is the center; w is the radius.
     *
     * material.x indexes SceneData::materials; the other material lanes are reserved.
     * For Schwarzschild, the radius is the fixed gravity-region cutoff, not the horizon.
     */
    struct SphereData
    {
        Float4 centerRadius;
        Int4 material;
    };

    /**
     * @brief GPU material record with indexed textures rather than owning pointers.
     *
     * kindTexture.x selects MaterialKind and y indexes the base texture.
     * parameters.x stores metal fuzz, dielectric IOR, or environment intensity.
     * Thermal emitters use parameters.yzw for kelvin, emission scale, and limb darkening.
     * layers contains night, cloud, normal, and specular texture indices; -1 means absent.
     */
    struct MaterialData
    {
        Int4 kindTexture;
        Float4 parameters;
        Int4 layers{-1, -1, -1, -1}; // Night, clouds, tangent-space normal, specular mask.
    };

    /**
     * @brief GPU texture record; kindChildren.x selects TextureKind.
     *
     * Checker children are y (odd) and z (even); color.xyz stores constant linear RGB.
     * image.xyzw stores offset, width, height, and storage encoding: 0 = float RGB,
     * 1 = packed sRGB RGBA8, 2 = packed linear RGBA8 (normal maps and masks).
     */
    struct TextureData
    {
        Int4 kindChildren;
        Float4 color;
        Int4 image; // offset, width, height
    };

    /**
     * @brief Thin emitting annulus centered on the Schwarzschild sphere.
     *
     * Radii use horizon units, peakTemperature is in kelvin, and normal defines its plane.
     * The temperature profile and circular motion are shading models, not a fluid solver.
     */
    struct AccretionDisk
    {
        bool enabled = false;
        float innerRadius = 3.0f;
        float outerRadius = 5.2f;
        float peakTemperature = 6000.0f;
        float emissionScale = 0.35f;
        Vec3 normal{0, 1, 0};
    };

    /**
     * @brief Own the indexed scene and texture pixels shared by both renderers.
     *
     * Sphere indices also identify editable bodies and animation tracks; keep them stable.
     */
    struct SceneData
    {
        std::vector<SphereData> spheres;
        std::vector<MaterialData> materials;
        std::vector<TextureData> textures;
        std::vector<Float4> texels;             ///< Linear float texels in original image row order.
        std::vector<std::uint32_t> imageTexels; ///< Packed RGBA8; TextureData::image.w selects the encoding.
        AccretionDisk disk;
        float atmosphereHeight = 0.045f; ///< Earth atmosphere thickness in horizon radii.

        /**
         * @brief Check scene indices, texture graphs, image bounds, and physical parameters.
         * @throws std::runtime_error If data is invalid or more than one black hole is present.
         */
        void validate() const;
    };

    /**
     * @brief Rendering quality and observer settings, independent of camera navigation.
     *
     * Velocity describes a local physical observer; it does not animate the camera.
     */
    struct RenderSettings
    {
        /**
         * @brief Choose a finite gravity region, scene-wide integration, or an error-tested cutoff.
         */
        enum IntegrationMode
        {
            FixedRadius,
            FullScene,
            AdaptiveCutoff
        };

        IntegrationMode integrationMode = FixedRadius;
        int width = 1440;
        int height = 900;
        int samples = 30;       ///< Target completed samples per pixel.
        std::uint32_t seed = 1; ///< Base seed for deterministic pixel/sample streams.
        bool redshift = true;
        float exposure = 1.0f; ///< Linear brightness multiplier applied before tone mapping.

        /**
         * @brief Choose the local frame relative to which observerVelocity is measured.
         *
         * Hovering is stationary outside the horizon and invalid at or inside it.
         * FreelyFalling uses the inward radial frame falling from rest at infinity.
         */
        enum ObserverType
        {
            Hovering,
            FreelyFalling
        };

        ObserverType observerType = FreelyFalling;
        Vec3 observerVelocity{0, 0, 0}; ///< Signed view-local right/up/forward components, in units of c.
        bool useObserverFrame = true;   ///< Apply the physical frame; see observerSettings().

        float relativeTolerance = 1e-4f; ///< Dimensionless RK4 error tolerance.
        float absoluteTolerance = 1e-6f; ///< Absolute error floor for position and affine tangent.
        float maxStep = 0.05f; ///< Near-hole affine step scale; grows with squared radius beyond r=3.
        int maxIntegrationAttempts = 16384; ///< Accepted plus rejected steps per gravity traversal.

        /**
         * @brief Check dimensions, samples, tolerances, mode, exposure, and speed (at most 0.999c).
         * @throws std::runtime_error If validation fails.
         */
        void validate() const;
    };

    /**
     * @brief Editable pinhole camera; positions use horizon radii and FOV uses degrees.
     *
     * Navigation changes the pose independently of the physical observer velocity.
     */
    struct CameraData
    {
        Vec3 position{4, 3, 10};
        Vec3 lookAt{2, 0, 0};
        Vec3 up{0, 1, 0};
        double verticalFov = 75; ///< Vertical field of view in degrees.

        /**
         * @brief Translate position and lookAt together.
         * @param direction Local right/up/forward input, normalized before movement.
         * @param distance Travel distance in horizon-radius units. Up follows the camera up vector.
         */
        void moveLocal(const Vec3 &direction, double distance);

        /**
         * @brief Rotate the look direction while preserving target distance.
         * @param yaw Horizontal angle in radians.
         * @param pitch Vertical angle in radians; elevation is clamped to +/-89 degrees.
         */
        void rotateView(double yaw, double pitch);

        /**
         * @brief Reject nonfinite values, an invalid FOV, or a degenerate view/up basis.
         * @throws std::runtime_error If validation fails.
         */
        void validate() const;

        /**
         * @brief Build the pinhole image plane at unit distance from the camera.
         * @param aspect Image width divided by height; must be positive and finite.
         * @return Origin, lower-left corner, full horizontal span, and full vertical span.
         * A pixel (u,v) points along corner + u*horizontal + v*vertical - origin.
         * @throws std::runtime_error If the camera or aspect ratio is invalid.
         */
        [[nodiscard]] std::array<Vec3, 4> basis(double aspect) const;
    };

    /**
     * @brief Load the illustrated black hole, Sun, Earth, Moon, and background scene.
     * @param assets Asset root containing the textures directory.
     * @throws std::runtime_error If a required texture cannot be read or decoded.
     */
    SceneData defaultScene(const std::filesystem::path &assets);

    /**
     * @brief Convert view-local velocity into world-aligned local-frame components.
     *
     * Preserves the legacy exterior camera mapping for a zero-velocity Hovering observer.
     * This prepares a copy for tracing; it does not change the selected observer type.
     */
    RenderSettings observerSettings(const SceneData &scene, const RenderSettings &settings,
                                    const CameraData &camera);

    /**
     * @brief Decode a normalized sRGB channel into linear light.
     */
    float decodeSrgb(float value);

    /**
     * @brief Clamp linear light to [0,1] and encode an sRGB byte; nonfinite input becomes zero.
     */
    unsigned char encodeSrgb(double value);

    /**
     * @brief Apply exposure and the display tone curve to one HDR linear channel.
     */
    double toneMap(double value, double exposure = 1.0);

    /**
     * @brief Apply exposure, the local bloom filter, and tone mapping at pixel (x,y).
     *
     * Input is averaged linear RGB in bottom-row-first order; output is linear display RGB.
     */
    Vec3 displayColor(const std::vector<Float4> &linear, int width, int height, int x, int y,
                      float exposure = 1.0f);

    /**
     * @brief Return the shared 2049-entry, logarithmic temperature-to-linear-RGB table.
     *
     * Temperatures span 500 to 50,000,000 kelvin; brightness is relative to a 5778 K source.
     */
    const std::vector<Float4> &blackbodyTable();

    /**
     * @brief Convert averaged, bottom-row-first linear pixels and write a display-ready PNG.
     *
     * Includes exposure, bloom, tone mapping, sRGB encoding, and vertical row reversal.
     */
    void savePng(const std::filesystem::path &path, int width, int height, const std::vector<Float4> &linear,
                 float exposure = 1.0f);

    /**
     * @brief Convert averaged linear pixels into tightly packed, top-row-first sRGB RGBA8.
     *
     * Uses the same display transform as display.frag; alpha is opaque.
     */
    std::vector<unsigned char> displayRgba(const std::vector<Float4> &linear, int width, int height,
                                           float exposure = 1.0f);

    /**
     * @brief Write tightly packed, top-row-first RGBA8 pixels without another color conversion.
     * @throws std::runtime_error If dimensions, byte count, or PNG writing are invalid.
     */
    void saveRgbaPng(const std::filesystem::path &path, int width, int height,
                     const std::vector<unsigned char> &rgba);
} // namespace rt
