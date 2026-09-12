/**
 * @file
 * @brief Frame timing, asynchronous output sinks, and event-loop export orchestration.
 */
#pragma once
#include "SceneData.h"
#include <functional>
#include <memory>

namespace rt
{
    /**
     * @brief Supported output containers: a PNG directory or an MP4 file.
     */
    enum class ExportFormat
    {
        PngSequence,
        Mp4
    };

    /**
     * @brief Export dimensions, duration, rate, and output location.
     *
     * bitrate is bits per second for MP4; fps is frames per second. The destination must
     * not exist. MP4 requires even pixel dimensions.
     */
    struct ExportSpec
    {
        ExportFormat format = ExportFormat::Mp4;
        std::filesystem::path destination;
        int width = 1920, height = 1080, frames = 300, fps = 30, bitrate = 20000000;

        /**
         * @brief Check dimensions, frame count, rate, and MP4-specific constraints.
         * @throws std::runtime_error For unsupported values; the destination is not checked.
         */
        void validateSettings() const;

        /**
         * @brief Check settings and require a nonempty, unused destination.
         * @throws std::runtime_error If export cannot start with these settings.
         */
        void validate() const;
    };

    /**
     * @brief Owned top-row-first sRGB RGBA8 frame with rational timing.
     *
     * stride is bytes per row, index is zero-based, and frame duration in seconds is
     * timeNumerator/timeDenominator. The buffer is transferred only on successful submission.
     */
    struct TimedFrame
    {
        int width = 0, height = 0, stride = 0, index = 0;
        int timeNumerator = 1, timeDenominator = 30;
        std::vector<unsigned char> rgba;

        /**
         * @brief Convert a frame boundary to integer ticks without accumulating rounded durations.
         * @param unitsPerSecond Destination clock rate, e.g. 10,000,000 for 100 ns ticks.
         * @param offset 0 for the frame start, 1 for the end; timeDenominator must be positive.
         */
        std::int64_t boundary(std::int64_t unitsPerSecond, int offset = 0) const;
    };

    /**
     * @brief Output lifecycle; Complete, Cancelled, and Failed are terminal states.
     */
    enum class SinkState
    {
        Starting,
        Ready,
        Finishing,
        Complete,
        Cancelled,
        Failed
    };

    /**
     * @brief Snapshot returned by the sink: lifecycle, written frame count, and failure message.
     */
    struct SinkProgress
    {
        SinkState state = SinkState::Starting;
        int written = 0;
        std::string error;
    };

    /**
     * @brief Asynchronous output boundary with bounded buffering and retryable submission.
     *
     * No OpenGL or platform encoder types cross this interface. The consumer owns submitted
     * frames; the producer keeps a frame when submit returns false and retries it later.
     */
    class FrameSink
    {
    public:
        /**
         * @brief Destroy the sink through its platform-independent interface.
         */
        virtual ~FrameSink() = default;

        /**
         * @brief Start an output session; preparation may continue asynchronously.
         */
        virtual void begin(const ExportSpec &) = 0;

        /**
         * @brief Transfer the frame buffer on success; leave it with the caller on backpressure.
         *
         * Returns false when the sink cannot currently accept another frame.
         */
        virtual bool submit(TimedFrame &) = 0;

        /**
         * @brief Return a synchronized progress snapshot without waiting for encoding to finish.
         */
        virtual SinkProgress poll() const = 0;

        /**
         * @brief Request finalization after queued frames are written; observe completion with poll.
         */
        virtual void finish() = 0;

        /**
         * @brief Request cancellation and cleanup; completion is reported through poll.
         */
        virtual void cancel() = 0;
    };

    /**
     * @brief Create the desktop PNG/video writer; platform encoder details remain private.
     */
    std::unique_ptr<FrameSink> makeDesktopFrameSink();

    /**
     * @brief Callbacks used by FrameExport to begin, await, and read a complete frame.
     *
     * beginFrame takes a zero-based frame number. completed must be nonblocking.
     * readFrame returns top-row-first sRGB RGBA8 at the export dimensions.
     */
    struct ExportRenderer
    {
        std::function<void(int)> beginFrame;
        std::function<bool()> completed;
        std::function<std::vector<unsigned char>()> readFrame;
    };

    /**
     * @brief Drive a renderer and an output sink from the application event loop.
     *
     * Retains a completed frame while the sink is full, preventing dropped frames.
     */
    class FrameExport
    {
    public:
        /**
         * @brief Validate the job and start the sink, taking ownership of it and the callbacks.
         */
        FrameExport(ExportSpec spec, ExportRenderer renderer, std::unique_ptr<FrameSink> sink);

        /**
         * @brief Request cancellation if the output session has not terminated.
         */
        ~FrameExport();

        /**
         * @brief Advance export state from the event loop; call repeatedly between UI events.
         *
         * Rendering is scheduled through callbacks; completed pixels are read only once.
         */
        void tick();

        /**
         * @brief Discard any pending submission and request sink cancellation.
         */
        void cancel();

        /**
         * @brief Whether the sink has completed, failed, or acknowledged cancellation.
         */
        bool done() const;

        /**
         * @brief Return the current sink status, written frame count, and error.
         */
        SinkProgress progress() const;

        /**
         * @brief Return the zero-based next frame index, or the frame count after all submissions.
         */
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

    /**
     * @brief Exercise export orchestration and desktop sinks using temporary output in directory.
     */
    int runExportTests(const std::filesystem::path &directory);

    /**
     * @brief Check frame rendering/readback and output timing with a live OpenGL renderer.
     */
    int runExportGpuTests(const std::filesystem::path &assets, const std::filesystem::path &directory);
} // namespace rt
