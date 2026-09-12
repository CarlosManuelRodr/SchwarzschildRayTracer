#pragma once
#include "SceneData.h"
#include <algorithm>
#include <cmath>

namespace rt
{
// Logical window coordinates; framebuffer scaling belongs only at the render boundary.
struct ViewportRect
{
    float x = 0, y = 0, width = 0, height = 0;

    bool contains(float px, float py) const
    {
        return width > 0 && height > 0 && px >= x && py >= y && px < x + width && py < y + height;
    }

    Vec3 imagePoint(float px, float py) const
    {
        return {(px - x) / width, 1 - (py - y) / height, 0};
    }

    ViewportRect fit(double aspect) const
    {
        auto r = *this;
        if (width <= 0 || height <= 0 || aspect <= 0)
            return r;
        if (width / height > aspect)
        {
            r.width = float(height * aspect);
            r.x += (width - r.width) / 2;
        }
        else
        {
            r.height = float(width / aspect);
            r.y += (height - r.height) / 2;
        }
        return r;
    }

    std::array<int, 2> pixels(float scaleX, float scaleY) const
    {
        return {std::max(1, int(std::lround(width * scaleX))),
                std::max(1, int(std::lround(height * scaleY)))};
    }
};

inline Vec3 editObserverComponent(Vec3 velocity, int axis, double requested)
{
    double c[] = {velocity.x, velocity.y, velocity.z};
    double other = 0;
    for (int i = 0; i < 3; ++i)
        if (i != axis)
            other += c[i] * c[i];
    double limit = std::sqrt(std::max(0.0, 0.999 * 0.999 - other));
    c[axis] = std::clamp(requested, -limit, limit);
    return {c[0], c[1], c[2]};
}
} // namespace rt
