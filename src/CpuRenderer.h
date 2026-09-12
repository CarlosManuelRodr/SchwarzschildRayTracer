/**
 * @file
 * @brief Double-precision reference rendering and geodesic picking.
 */
#pragma once
#include "SceneData.h"
#include <cstdint>

namespace rt
{
    /**
     * @brief Terminal ray diagnostic from the shared tracing core.
     *
     * color is linear radiance; position is the final world position; direction is the
     * affine spatial tangent (not necessarily unit length). Status is 1 = completed,
     * 2 = numerical failure, 3 = capture or unavailable physical source/frame.
     * attempts counts integration attempts in the last gravity traversal.
     */
    struct RayResult
    {
        Vec3 color, position, direction;
        int status = 0, attempts = 0;
    };

    /**
     * @brief Trace one diagnostic ray to completion using double precision.
     *
     * Inputs must already be valid. With no camera basis, observerVelocity uses
     * world-aligned local-frame components; call observerSettings for view-local input.
     * The seed initializes a deterministic stream shared with the GPU algorithm.
     */
    RayResult traceCpu(const SceneData &, const RenderSettings &, const Vec3 &origin, const Vec3 &direction,
                       std::uint32_t seed = 1);

    /**
     * @brief Render all requested samples using CPU workers and deterministic pixel streams.
     *
     * Returns averaged linear RGB in bottom-row-first order, with sample count in w.
     * The optional failures pointer receives the number of numerically failed rays.
     * Scene, settings, and camera are validated before rendering.
     */
    std::vector<Float4> renderCpu(const SceneData &, const RenderSettings &, const CameraData &,
                                  std::uint64_t* failures = nullptr);

    /**
     * @brief Trace normalized image coordinates (u,v) to the first editable body.
     *
     * Coordinates start at the lower left. Returns a stable sphere index or -1 for sky.
     * The accretion disk and captured rays select the black hole; lighting is skipped.
     */
    int pickBody(const SceneData &, const RenderSettings &, const CameraData &, double u, double v);

    /**
     * @brief Run the CPU geometry, material, observer, and integration regression suite.
     */
    int runCpuTests();

    /**
     * @brief Compare GPU output with the CPU reference using the supplied shader assets.
     */
    int runGpuTests(const std::filesystem::path &assets);
} // namespace rt
