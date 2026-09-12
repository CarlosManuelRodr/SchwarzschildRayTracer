#include "FrameExport.h"
#include <stdexcept>

namespace rt
{
    void ExportSpec::validateSettings() const
    {
        if (width < 1 || height < 1 || width > 16384 || height > 16384 || frames < 1 || frames > 1000000 ||
            fps < 1 || fps > 240)
            throw std::runtime_error("Invalid export size, frame count or FPS");

        if (format == ExportFormat::Mp4 &&
            ((width % 2) || (height % 2) || bitrate < 100000 || bitrate > 200000000))
            throw std::runtime_error("MP4 needs even dimensions and a bitrate between 0.1 and 200 Mbps");
    }

    void ExportSpec::validate() const
    {
        validateSettings();

        if (destination.empty())
            throw std::runtime_error("Choose an export destination");

        if (std::filesystem::exists(destination))
            throw std::runtime_error("Export destination already exists; choose a new name");
    }

    std::int64_t TimedFrame::boundary(std::int64_t units, int offset) const
    {
        return (std::int64_t(index) + offset) * timeNumerator * units / timeDenominator;
    }

    FrameExport::FrameExport(ExportSpec s, ExportRenderer r, std::unique_ptr<FrameSink> writer)
        : spec(std::move(s)), renderer(std::move(r)), sink(std::move(writer))
    {
        spec.validate();
        sink->begin(spec);
    }

    FrameExport::~FrameExport()
    {
        if (!done())
            sink->cancel();
    }

    SinkProgress FrameExport::progress() const
    {
        return sink->poll();
    }

    bool FrameExport::done() const
    {
        auto s = progress().state;

        return s == SinkState::Complete || s == SinkState::Cancelled || s == SinkState::Failed;
    }

    void FrameExport::cancel()
    {
        cancelling = true;
        pending = {};
        sink->cancel();
    }

    void FrameExport::tick()
    {
        if (done() || cancelling || finalizing || progress().state != SinkState::Ready)
            return;

        if (!rendering && !ready)
        {
            renderer.beginFrame(index);
            rendering = true;

            return;
        }

        if (rendering && renderer.completed())
        {
            pending = {spec.width, spec.height, spec.width * 4, index, 1, spec.fps, renderer.readFrame()};
            rendering = false;
            ready = true;
        }

        if (ready && sink->submit(pending))
        {
            ready = false;

            if (++index == spec.frames)
            {
                finalizing = true;
                sink->finish();
            }
        }
    }
} // namespace rt
