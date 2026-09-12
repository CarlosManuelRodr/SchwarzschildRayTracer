#include "FrameExport.h"
#include "SdlSupport.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif

namespace rt
{
    namespace
    {
        class VideoWriter
        {
        public:
            virtual ~VideoWriter() = default;
            virtual void write(const TimedFrame &) = 0;
            virtual void finish() = 0;
        };
#ifdef _WIN32
        using Microsoft::WRL::ComPtr;

        void checkHr(HRESULT hr, const char* action)
        {
            if (FAILED(hr))
            {
                std::ostringstream s;
                s << action << " (Media Foundation HRESULT 0x" << std::hex << unsigned(hr)
                  << "). Check Windows media features and encoder support, or export PNG frames.";
                throw std::runtime_error(s.str());
            }
        }

        class MediaFoundationWriter final : public VideoWriter
        {
            ComPtr<IMFSinkWriter> writer;
            DWORD stream = 0;

        public:
            MediaFoundationWriter(const ExportSpec &s, const std::filesystem::path &path)
            {
                checkHr(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer),
                        "Create MP4 writer");
                ComPtr<IMFMediaType> output, input;
                checkHr(MFCreateMediaType(&output), "Create output type");
                checkHr(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set output major type");
                checkHr(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Select H.264");
                checkHr(output->SetUINT32(MF_MT_AVG_BITRATE, s.bitrate), "Set bitrate");
                checkHr(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
                        "Set progressive output");
                checkHr(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, s.width, s.height),
                        "Set output size");
                checkHr(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, s.fps, 1), "Set output FPS");
                checkHr(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
                        "Set pixel aspect");
                checkHr(writer->AddStream(output.Get(), &stream), "Add H.264 stream");
                checkHr(MFCreateMediaType(&input), "Create input type");
                checkHr(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set input major type");
                checkHr(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Select RGB32 input");
                checkHr(input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
                        "Set progressive input");
                checkHr(input->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(-s.width * 4)),
                        "Set bottom-up RGB stride");
                checkHr(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, s.width, s.height),
                        "Set input size");
                checkHr(MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, s.fps, 1), "Set input FPS");
                checkHr(MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
                        "Set input pixel aspect");
                checkHr(writer->SetInputMediaType(stream, input.Get(), nullptr), "Configure H.264 encoder");
                checkHr(writer->BeginWriting(), "Start MP4 encoding");
            }

            void write(const TimedFrame &f) override
            {
                ComPtr<IMFMediaBuffer> buffer;
                ComPtr<IMFSample> sample;
                DWORD size = DWORD(f.rgba.size());
                checkHr(MFCreateMemoryBuffer(size, &buffer), "Allocate video sample");
                BYTE* data = nullptr;
                checkHr(buffer->Lock(&data, nullptr, nullptr), "Lock video sample");
                // RGB32 is BGRX in memory. A negative stride describes bottom-up rows.
                for (int y = 0; y < f.height; ++y)
                    for (int x = 0; x < f.width; ++x)
                    {
                        auto src = std::size_t(y) * f.stride + x * 4;
                        auto dst = std::size_t(f.height - 1 - y) * f.stride + x * 4;
                        data[dst] = f.rgba[src + 2];
                        data[dst + 1] = f.rgba[src + 1];
                        data[dst + 2] = f.rgba[src];
                        data[dst + 3] = 255;
                    }
                checkHr(buffer->Unlock(), "Unlock video sample");
                checkHr(buffer->SetCurrentLength(size), "Set sample size");
                checkHr(MFCreateSample(&sample), "Create video sample");
                checkHr(sample->AddBuffer(buffer.Get()), "Attach sample buffer");
                checkHr(sample->SetSampleTime(f.boundary(10000000)), "Set sample timestamp");
                checkHr(sample->SetSampleDuration(f.boundary(10000000, 1) - f.boundary(10000000)),
                        "Set sample duration");
                checkHr(writer->WriteSample(stream, sample.Get()), "Encode video frame");
            }

            void finish() override
            {
                checkHr(writer->Finalize(), "Finalize MP4");
            }
        };

        struct MediaRuntime
        {
            bool com = false, mf = false;

            explicit MediaRuntime(bool needed)
            {
                if (!needed)
                    return;

                checkHr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize COM");
                com = true;
                HRESULT hr = MFStartup(MF_VERSION);

                if (FAILED(hr))
                {
                    CoUninitialize();
                    com = false;
                    checkHr(hr, "Start Media Foundation");
                }

                mf = true;
            }

            ~MediaRuntime()
            {
                if (mf)
                    MFShutdown();
                if (com)
                    CoUninitialize();
            }
        };
#else
        class FfmpegWriter final : public VideoWriter
        {
            SDL_Process* process = nullptr;
            SDL_IOStream* input = nullptr;
            std::thread diagnostics;
            std::thread cancellation;
            std::atomic<bool> stopWatching{false};
            std::string messages;

            void drain()
            {
                auto* output = SDL_GetProcessOutput(process);
                char bytes[4096];

                while (output)
                {
                    auto n = SDL_ReadIO(output, bytes, sizeof(bytes));

                    if (!n)
                    {
                        if (SDL_GetIOStatus(output) == SDL_IO_STATUS_NOT_READY)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            continue;
                        }

                        break;
                    }

                    messages.append(bytes, n);

                    if (messages.size() > 16384)
                        messages.erase(0, messages.size() - 16384);
                }
            }

            static SDL_Process* launch(const std::vector<std::string> &args)
            {
                std::vector<const char*> argv;

                for (const auto &a : args)
                    argv.push_back(a.c_str());
                argv.push_back(nullptr);
                auto props = SDL_CreateProperties();
                SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
                SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
                SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
                SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
                auto* p = SDL_CreateProcessWithProperties(props);
                SDL_DestroyProperties(props);

                if (!p)
                    throw std::runtime_error(std::string("Cannot launch ffmpeg from PATH: ") +
                                             SDL_GetError());
                return p;
            }

        public:
            FfmpegWriter(const ExportSpec &s, const std::filesystem::path &path,
                         const std::atomic<bool> &cancelled)
            {
                // Verify libx264 before any expensive rendering.
                auto* probe = launch({"ffmpeg", "-hide_banner", "-encoders"});
                std::size_t size = 0;
                int code = 0;
                void* data = SDL_ReadProcess(probe, &size, &code);
                std::string encoders = data ? std::string(static_cast<char*>(data), size) : "";
                SDL_free(data);
                SDL_DestroyProcess(probe);

                if (code || encoders.find("libx264") == std::string::npos)
                    throw std::runtime_error(
                        "ffmpeg on PATH must include the libx264 encoder; PNG export is available");
                if (cancelled.load())
                    throw std::runtime_error("Export cancelled");
                // Preflight the actual size, rate, bitrate, encoder and muxer using one synthetic
                // frame. No scene frames are rendered until this process succeeds.
                auto preflight = path.parent_path() / "probe.mp4";
                probe = launch({"ffmpeg",
                                "-hide_banner",
                                "-loglevel",
                                "error",
                                "-n",
                                "-f",
                                "lavfi",
                                "-i",
                                "color=black:size=" + std::to_string(s.width) + "x" +
                                    std::to_string(s.height) + ":rate=" + std::to_string(s.fps),
                                "-frames:v",
                                "1",
                                "-an",
                                "-c:v",
                                "libx264",
                                "-pix_fmt",
                                "yuv420p",
                                "-b:v",
                                std::to_string(s.bitrate),
                                "-f",
                                "mp4",
                                preflight.string()});
                data = SDL_ReadProcess(probe, &size, &code);
                std::string errors = data ? std::string(static_cast<char*>(data), size) : "";
                SDL_free(data);
                SDL_DestroyProcess(probe);
                std::error_code ignored;
                std::filesystem::remove(preflight, ignored);

                if (code)
                    throw std::runtime_error("FFmpeg configuration unavailable: " + errors);

                if (cancelled.load())
                    throw std::runtime_error("Export cancelled");

                process = launch({"ffmpeg",
                                  "-hide_banner",
                                  "-loglevel",
                                  "error",
                                  "-n",
                                  "-f",
                                  "rawvideo",
                                  "-pixel_format",
                                  "rgba",
                                  "-video_size",
                                  std::to_string(s.width) + "x" + std::to_string(s.height),
                                  "-framerate",
                                  std::to_string(s.fps),
                                  "-i",
                                  "pipe:0",
                                  "-an",
                                  "-c:v",
                                  "libx264",
                                  "-pix_fmt",
                                  "yuv420p",
                                  "-b:v",
                                  std::to_string(s.bitrate),
                                  "-movflags",
                                  "+faststart",
                                  "-f",
                                  "mp4",
                                  path.string()});
                input = SDL_GetProcessInput(process);
                diagnostics = std::thread(
                    [this]
                    {
                        drain();
                    });
                cancellation = std::thread(
                    [this, &cancelled]
                    {
                        while (!stopWatching.load())
                        {
                            if (cancelled.load())
                            {
                                SDL_KillProcess(process, true);

                                return;
                            }

                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                    });
            }

            ~FfmpegWriter()
            {
                stopWatching = true;

                if (cancellation.joinable())
                    cancellation.join();
                if (process)
                {
                    SDL_KillProcess(process, true);

                    if (diagnostics.joinable())
                        diagnostics.join();
                    SDL_DestroyProcess(process);
                }
            }

            void write(const TimedFrame &f) override
            {
                std::size_t written = 0;

                while (written < f.rgba.size())
                {
                    auto n = SDL_WriteIO(input, f.rgba.data() + written, f.rgba.size() - written);

                    if (!n)
                        throw std::runtime_error("FFmpeg stopped accepting frames");

                    written += n;
                }
            }

            void finish() override
            {
                SDL_CloseIO(input);
                input = nullptr;
                int code = 0;
                SDL_WaitProcess(process, true, &code);

                if (diagnostics.joinable())
                    diagnostics.join();
                if (code)
                    throw std::runtime_error("FFmpeg export failed: " + messages);
            }
        };
#endif

        class DesktopFrameSink final : public FrameSink
        {
            mutable std::mutex mutex;
            std::condition_variable wake;
            std::deque<TimedFrame> queue;
            SinkProgress status;
            ExportSpec spec;
            std::thread worker;
            bool finishRequested = false;
            std::atomic<bool> cancelRequested{false};
            int accepted = 0;

            void setState(SinkState s, const std::string &error = {})
            {
                std::lock_guard<std::mutex> lock(mutex);
                status.state = s;
                status.error = error;
            }

            void run()
            {
                std::filesystem::path temporary;
                bool ownsTemporary = false;

                try
                {
                    spec.validate();
                    auto parent = spec.destination.parent_path();

                    if (!parent.empty())
                        std::filesystem::create_directories(parent);
#ifdef _WIN32
                    MediaRuntime runtime(spec.format == ExportFormat::Mp4);
#endif
                    std::unique_ptr<VideoWriter> video;

                    if (spec.format == ExportFormat::PngSequence)
                    {
                        if (!std::filesystem::create_directory(spec.destination))
                            throw std::runtime_error("PNG directory already exists");
                    }
                    else
                    {
                        // Reserve a unique sibling directory, avoiding any overwrite race for temporary data.
                        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                        temporary = spec.destination;
                        temporary += ".partial-" + std::to_string(stamp);

                        if (!std::filesystem::create_directory(temporary))
                            throw std::runtime_error("Cannot reserve temporary export directory");

                        ownsTemporary = true;
                        auto file = temporary / "video.mp4";
#ifdef _WIN32
                        video = std::make_unique<MediaFoundationWriter>(spec, file);
#else
                        video = std::make_unique<FfmpegWriter>(spec, file, cancelRequested);
#endif
                    }

                    setState(SinkState::Ready);

                    for (;;)
                    {
                        TimedFrame f;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            wake.wait(lock,
                                      [&]
                                      {
                                          return cancelRequested || !queue.empty() || finishRequested;
                                      });
                            if (cancelRequested)
                                break;
                            if (queue.empty())
                                break;
                            f = std::move(queue.front());
                            queue.pop_front();
                        }

                        if (video)
                            video->write(f);
                        else
                        {
                            std::ostringstream name;
                            name << "frame-" << std::setw(6) << std::setfill('0') << f.index << ".png";
                            saveRgbaPng(spec.destination / name.str(), spec.width, spec.height, f.rgba);
                        }

                        {
                            std::lock_guard<std::mutex> lock(mutex);
                            ++status.written;
                        }
                    }

                    bool cancelled;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        cancelled = cancelRequested;
                    }

                    if (!cancelled)
                    {
                        setState(SinkState::Finishing);

                        if (video)
                            video->finish();
                    }

                    video.reset();
                    // Cancellation can arrive during a blocking encoder finalization.
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        cancelled = cancelRequested;
                    }

                    if (!cancelled && !temporary.empty())
                    {
                        // Publish without replacing a file created since export began.
#ifdef _WIN32
                        if (!MoveFileExW((temporary / L"video.mp4").c_str(), spec.destination.c_str(),
                                         MOVEFILE_WRITE_THROUGH))
                            throw std::runtime_error(
                                "Cannot publish MP4 (destination exists or is unavailable), Windows error " +
                                std::to_string(GetLastError()));
#else
                        std::filesystem::create_hard_link(temporary / "video.mp4", spec.destination);
#endif
                    }

                    if (ownsTemporary)
                    {
                        std::filesystem::remove(temporary / "video.mp4");
                        std::filesystem::remove(temporary);
                    }

                    setState(cancelled ? SinkState::Cancelled : SinkState::Complete);
                }
                catch (const std::exception &e)
                {
                    if (ownsTemporary)
                    {
                        std::error_code ignored;
                        std::filesystem::remove(temporary / "video.mp4", ignored);
                        std::filesystem::remove(temporary / "probe.mp4", ignored);
                        std::filesystem::remove(temporary, ignored);
                    }

                    setState(cancelRequested.load() ? SinkState::Cancelled : SinkState::Failed,
                             cancelRequested.load() ? "" : e.what());
                }
            }

        public:
            ~DesktopFrameSink() override
            {
                cancel();

                if (worker.joinable())
                    worker.join();
            }

            void begin(const ExportSpec &s) override
            {
                if (worker.joinable())
                    throw std::runtime_error("Frame sink already started");

                s.validate();
                spec = s;
                worker = std::thread(
                    [this]
                    {
                        run();
                    });
            }

            bool submit(TimedFrame &f) override
            {
                std::lock_guard<std::mutex> lock(mutex);

                if (status.state != SinkState::Ready || queue.size() >= 2 || finishRequested ||
                    cancelRequested)
                    return false;

                if (f.index != accepted || accepted >= spec.frames || f.width != spec.width ||
                    f.height != spec.height || f.stride != spec.width * 4 || f.timeNumerator != 1 ||
                    f.timeDenominator != spec.fps || f.rgba.size() != std::size_t(f.stride) * f.height)
                    throw std::runtime_error("Invalid or out-of-order export frame");

                queue.push_back(std::move(f));
                ++accepted;
                wake.notify_one();

                return true;
            }

            SinkProgress poll() const override
            {
                std::lock_guard<std::mutex> lock(mutex);

                return status;
            }

            void finish() override
            {
                std::lock_guard<std::mutex> lock(mutex);

                if (accepted != spec.frames)
                    throw std::runtime_error("Export finished before all frames were submitted");

                finishRequested = true;
                status.state = SinkState::Finishing;
                wake.notify_one();
            }

            void cancel() override
            {
                std::lock_guard<std::mutex> lock(mutex);
                cancelRequested = true;
                queue.clear();
                wake.notify_one();
            }
        };
    } // namespace

    std::unique_ptr<FrameSink> makeDesktopFrameSink()
    {
        return std::make_unique<DesktopFrameSink>();
    }
} // namespace rt
