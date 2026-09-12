/**
 * @file
 * @brief Logical viewport geometry and bounded observer-velocity editing.
 */
#pragma once
#include "SceneData.h"
#include <algorithm>
#include <cmath>

namespace rt
{
    /**
     * @brief Rectangle in logical window coordinates, with a top-left origin.
     *
     * Convert to framebuffer pixels only at the render boundary to support display scaling.
     */
    struct ViewportRect
    {
        float x = 0, y = 0, width = 0, height = 0;

        /**
         * @brief Test the half-open rectangle, excluding the right and bottom edges.
         */
        bool contains(float px, float py) const
        {
            return width > 0 && height > 0 && px >= x && py >= y && px < x + width && py < y + height;
        }

        /**
         * @brief Convert window coordinates to lower-left normalized image coordinates (u,v,0).
         *
         * Requires positive dimensions; coordinates outside the rectangle are not clamped.
         */
        Vec3 imagePoint(float px, float py) const
        {
            return {(px - x) / width, 1 - (py - y) / height, 0};
        }

        /**
         * @brief Center the largest rectangle with the requested width/height aspect ratio.
         *
         * Returns the original rectangle for nonpositive dimensions or aspect.
         */
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

        /**
         * @brief Scale and round logical dimensions to framebuffer pixels, at least one per axis.
         */
        std::array<int, 2> pixels(float scaleX, float scaleY) const
        {
            return {std::max(1, int(std::lround(width * scaleX))),
                    std::max(1, int(std::lround(height * scaleY)))};
        }
    };

    /**
     * @brief Edit one view-local velocity component without exceeding total speed 0.999c.
     * @param axis Component index: 0 = right, 1 = up, 2 = forward.
     * @param requested Signed component in units of c; clamped to the remaining speed budget.
     * Other components are unchanged; the incoming velocity must already be valid.
     */
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
