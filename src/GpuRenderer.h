#pragma once
#include "CpuRenderer.h"
#include <memory>
namespace rt {
struct GpuProgress {
    double meanSamples=0, lastBatchMilliseconds=0, maxBatchMilliseconds=0, totalGpuMilliseconds=0;
    std::uint64_t failures=0;
    bool finished=false;
};
class GpuRenderer {
public:
    explicit GpuRenderer(const std::filesystem::path& assets);
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&)=delete;
    GpuRenderer& operator=(const GpuRenderer&)=delete;
    void uploadScene(const SceneData& scene);
    void reset(const RenderSettings& settings,const CameraData& camera);
    std::array<int,2> fitResolution(int width,int height) const;
    bool dispatch(); // Nonblocking; at most one bounded batch in flight.
    bool poll();
    void present(int width,int height);
    std::vector<Float4> readback(); // Completed samples, bottom row first.
    std::vector<RayResult> traceRays(const SceneData&,const RenderSettings&,const std::vector<std::array<Vec3,2>>& rays);
    const GpuProgress& progress() const;
    const std::string& device() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
