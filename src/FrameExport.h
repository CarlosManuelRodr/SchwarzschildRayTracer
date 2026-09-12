#pragma once
#include "SceneData.h"
#include <functional>
#include <memory>

namespace rt
{
enum class ExportFormat
{
    PngSequence,
    Mp4
};

struct ExportSpec
{
    ExportFormat format = ExportFormat::Mp4;
    std::filesystem::path destination;
    int width = 1920, height = 1080, frames = 300, fps = 30, bitrate = 20000000;
    void validate() const;
};

struct TimedFrame
{
    int width = 0, height = 0, stride = 0, index = 0;
    int timeNumerator = 1, timeDenominator = 30;
    std::vector<unsigned char> rgba;
    std::int64_t boundary(std::int64_t unitsPerSecond, int offset = 0) const;
};
enum class SinkState
{
    Starting,
    Ready,
    Finishing,
    Complete,
    Cancelled,
    Failed
};

struct SinkProgress
{
    SinkState state = SinkState::Starting;
    int written = 0;
    std::string error;
};

// No OpenGL, native process, or COM types cross this boundary. Submission transfers
// ownership only on success; callers retry after backpressure without dropping frames.
class FrameSink
{
  public:
    virtual ~FrameSink() = default;
    virtual void begin(const ExportSpec&) = 0;
    virtual bool submit(TimedFrame&) = 0;
    virtual SinkProgress poll() const = 0;
    virtual void finish() = 0;
    virtual void cancel() = 0;
};

std::unique_ptr<FrameSink> makeDesktopFrameSink();

struct ExportRenderer
{
    std::function<void(int)> beginFrame;
    std::function<bool()> completed;
    std::function<std::vector<unsigned char>()> readFrame;
};

// Event-loop driven orchestration, independently testable with a fake renderer/sink.
class FrameExport
{
  public:
    FrameExport(ExportSpec spec, ExportRenderer renderer, std::unique_ptr<FrameSink> sink);
    ~FrameExport();
    void tick();
    void cancel();
    bool done() const;
    SinkProgress progress() const;

    int frame() const
    {
        return index;
    }

  private:
    ExportSpec spec;
    ExportRenderer renderer;
    std::unique_ptr<FrameSink> sink;
    TimedFrame pending;
    int index = 0;
    bool rendering = false, ready = false, finalizing = false, cancelling = false;
};

int runExportTests(const std::filesystem::path& directory);
int runExportGpuTests(const std::filesystem::path& assets, const std::filesystem::path& directory);
} // namespace rt
