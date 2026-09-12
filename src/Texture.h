/**
 * @file
 * @brief Historical CPU teaching implementation, retained for reference.
 *
 * The current application uses rt::SceneData and the shared TraceCore.inl kernel.
 */
#pragma once
#include "Vector3.h"

/**
 * @class Texture
 * @brief Abstract texture lookup interface.
 */
class Texture
{
public:
    /// @brief Return the texture color at a surface point.
    /// @param u Horizontal texture coordinate.
    /// @param v Vertical texture coordinate.
    /// @param p World-space surface point.
    virtual Vector3 Value(float u, float v, const Vector3 &p) const = 0;
};

/**
 * @class ConstantTexture
 * @brief Texture returning the same color at every point.
 */
class ConstantTexture : public Texture
{
public:
    Vector3 color;

    ConstantTexture(Vector3 textureColor) : color(textureColor) {}

    virtual Vector3 Value(float u, float v, const Vector3 &p) const
    {
        return color;
    }
};

/**
 * @class CheckerTexture
 * @brief Three-dimensional checker pattern selecting two borrowed child textures.
 */
class CheckerTexture : public Texture
{
public:
    Texture* odd;
    Texture* even;

    CheckerTexture(Texture* t0, Texture* t1) : even(t0), odd(t1) {}

    virtual Vector3 Value(float u, float v, const Vector3 &p) const
    {
        float sines = sin(1.0f * p.x()) * sin(1.0f * p.y()) * sin(1.0f * p.z());

        if (sines < 0)
            return odd->Value(u, v, p);
        else
            return even->Value(u, v, p);
    }
};

/**
 * @class ImageTexture
 * @brief Nearest-neighbor lookup into a borrowed, tightly packed RGB8 image.
 *
 * Coordinates clamp at image edges; this historical lookup does not decode sRGB.
 */
class ImageTexture : public Texture
{
public:
    int nx, ny;
    unsigned char* data;

    ImageTexture(unsigned char* pixels, int A, int B) : data(pixels), nx(A), ny(B) {}

    virtual Vector3 Value(float u, float v, const Vector3 &p) const
    {
        int i = int(u * nx);
        int j = int((1 - v) * ny - 0.001f);

        if (i < 0)
            i = 0;
        if (j < 0)
            j = 0;
        if (i > nx - 1)
            i = nx - 1;
        if (j > ny - 1)
            j = ny - 1;

        float r = int(data[3 * i + 3 * nx * j]) / 255.0f;
        float g = int(data[3 * i + 3 * nx * j + 1]) / 255.0f;
        float b = int(data[3 * i + 3 * nx * j + 2]) / 255.0f;

        return Vector3(r, g, b);
    }
};
