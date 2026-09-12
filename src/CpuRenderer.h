#pragma once
#include "SceneData.h"
#include <cstdint>

namespace rt
{
struct RayResult
{
    Vec3 color, position, direction;
    int status = 0, attempts = 0;
};

RayResult traceCpu(
    const SceneData&, const RenderSettings&, const Vec3 &origin, const Vec3 &direction, std::uint32_t seed = 1);
std::vector<Float4> renderCpu(const SceneData&,
                              const RenderSettings&,
                              const CameraData&,
                              std::uint64_t* failures = nullptr);
int pickBody(const SceneData&, const RenderSettings&, const CameraData&, double u, double v);
int runCpuTests();
int runGpuTests(const std::filesystem::path& assets);
} // namespace rt
