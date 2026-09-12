#include "Animation.h"
#include "Viewport.h"
#include "FrameExport.h"
#include "GpuRenderer.h"
#include "SdlSupport.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
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
void require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}

SceneData fixture()
{
    SceneData s;
    MaterialData m;
    m.kindTexture = {Earth, 0, 0, 0};
    s.materials.push_back(m);
    SphereData sphere;
    sphere.centerRadius = {0, 0, 0, 1};
    sphere.material = {0, 0, 0, 0};
    s.spheres.push_back(sphere);
    return s;
}

std::filesystem::path freshDirectory(const std::filesystem::path& root, const char* prefix)
{
    auto dir = root / (std::string(prefix) +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}

void waitExport(FrameExport& job)
{
    auto start = std::chrono::steady_clock::now();
    while (!job.done())
    {
        job.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "Export test timed out");
    }
    auto p = job.progress();
    if (p.state == SinkState::Failed)
        throw std::runtime_error(p.error);
}

struct FakeSink : FrameSink
{
    SinkProgress p;
    std::vector<int> received;
    int attempts = 0;

    void begin(const ExportSpec&) override
    {
        p.state = SinkState::Ready;
    }

    bool submit(TimedFrame& f) override
    {
        if (++attempts % 3)
            return false; // Force repeated backpressure.
        require(f.index == int(received.size()), "Out-of-order fake frame");
        require(f.boundary(10000000, 1) > f.boundary(10000000), "Non-increasing timestamp");
        received.push_back(f.index);
        f.rgba.clear();
        ++p.written;
        return true;
    }

    SinkProgress poll() const override
    {
        return p;
    }

    void finish() override
    {
        p.state = SinkState::Complete;
    }

    void cancel() override
    {
        p.state = SinkState::Cancelled;
    }
};

std::vector<unsigned char> colorFrame(int width, int height)
{
    std::vector<unsigned char> p(std::size_t(width) * height * 4, 255);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            auto i = std::size_t(y * width + x) * 4;
            p[i] = y < height / 2 ? 230 : 10;
            p[i + 1] = 20;
            p[i + 2] = y < height / 2 ? 10 : 230;
        }
    return p;
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;

void hr(HRESULT value)
{
    require(SUCCEEDED(value), "Media Foundation decode verification failed");
}

void verifyVideo(const std::filesystem::path& path, int width, int height, int frames, int fps)
{
    hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    hr(MFStartup(MF_VERSION));

    struct Runtime
    {
        ~Runtime()
        {
            MFShutdown();
            CoUninitialize();
        }
    } runtime;

    ComPtr<IMFAttributes> attributes;
    hr(MFCreateAttributes(&attributes, 1));
    hr(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
    ComPtr<IMFSourceReader> reader;
    hr(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader));
    ComPtr<IMFMediaType> type;
    hr(MFCreateMediaType(&type));
    hr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    hr(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    hr(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get()));
    type.Reset();
    hr(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &type));
    UINT32 w = 0, h = 0;
    hr(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h));
    require(int(w) == width && int(h) == height, "Wrong video resolution");
    UINT32 rawStride = 0;
    if (FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride)))
    {
        LONG stride = 0;
        hr(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &stride));
        rawStride = UINT32(stride);
    }
    LONG stride = LONG(rawStride);
    int count = 0;
    for (;;)
    {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr(reader->ReadSample(
            DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &timestamp, &sample));
        if (sample)
        {
            require(std::abs(timestamp - std::int64_t(count) * 10000000 / fps) < 10000,
                    "Wrong video frame timestamp");
            if (!count)
            {
                ComPtr<IMFMediaBuffer> buffer;
                hr(sample->ConvertToContiguousBuffer(&buffer));
                BYTE* bytes = nullptr;
                DWORD length = 0;
                hr(buffer->Lock(&bytes, nullptr, &length));
                std::size_t top =
                    std::size_t(stride < 0 ? height - 1 : 0) * std::abs(stride) + 4 * (width / 2);
                std::size_t bottom =
                    std::size_t(stride < 0 ? 0 : height - 1) * std::abs(stride) + 4 * (width / 2);
                bool correct = bottom + 2 < length && top + 2 < length && bytes[top + 2] > bytes[top] + 100 &&
                               bytes[bottom] > bytes[bottom + 2] + 100;
                buffer->Unlock();
                require(correct, "MP4 color channels or vertical orientation are wrong");
            }
            ++count;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }
    require(count == frames, "Wrong encoded frame count");
    PROPVARIANT duration;
    PropVariantInit(&duration);
    hr(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &duration));
    require(duration.vt == VT_UI8 &&
                std::abs(double(duration.uhVal.QuadPart) - double(frames) * 10000000 / fps) < 20000,
            "Wrong MP4 duration");
    PropVariantClear(&duration);
}
#endif
} // namespace

int runAnimationTests()
{
    ViewportRect area{100, 50, 800, 600};
    auto wide = area.fit(2.0);
    require(wide.x == 100 && wide.y == 150 && wide.width == 800 && wide.height == 400,
            "Viewport must letterbox fixed aspect ratios");
    require(!wide.contains(200, 100) && wide.contains(500, 350),
            "Picking must exclude letterboxing and panels");
    auto uv = wide.imagePoint(300, 250);
    require(uv.x == 0.25 && uv.y == 0.75,
            "Viewport picking must use its offset and bottom-up image coordinates");
    require(area.pixels(1.5f, 2.f) == std::array<int, 2>{1200, 1200},
            "Viewport resolution must use framebuffer DPI scaling");
    auto tall = area.fit(0.5);
    require(tall.x == 350 && tall.width == 300 && tall.height == 600, "Portrait images must be pillarboxed");
    auto velocity = editObserverComponent({0.2, 0.6, 0.3}, 0, -0.999);
    require(velocity.x < 0 && velocity.y == 0.6 && velocity.z == 0.3 &&
                std::abs(dot(velocity, velocity) - 0.999 * 0.999) < 1e-12,
            "Signed velocity editing must preserve other axes and cap combined speed");
    velocity = editObserverComponent(velocity, 0, 0);
    require(velocity.x == 0 && velocity.y == 0.6 && velocity.z == 0.3,
            "Velocity zero must be exact and preserve other axes");

    auto scene = fixture();
    CameraData camera;
    AnimationEditor e(scene, camera);
    e.selectBody(0);
    require(e.selectedBody() == 0, "Body track selection failed");
    e.selectBody(-1);
    require(!e.hasSelection() && e.selectedKey == -1, "Background must clear track and key selection");
    e.capture();
    e.remove();
    require(!e.retime(0, 1) && !e.canUndo(), "Deselected key commands must not mutate animation history");
    e.selectedTrack = 1;
    require(e.clip.frames == 300 && e.clip.fps == 30, "Wrong timeline defaults");
    require(e.clip.tracks[1].evaluate(100).position.x == 0, "Empty track base");
    scene.spheres[0].centerRadius.x = 2;
    e.observe(scene, camera);
    e.endGesture();
    e.capture();
    require(e.clip.tracks[1].evaluate(100).position.x == 2, "Single key hold");
    e.seek(10);
    scene.spheres[0].centerRadius.x = 12;
    e.observe(scene, camera);
    e.endGesture();
    require(e.hasDrafts(), "Animated edit must be a draft");
    e.selectBody(-1);
    require(e.hasDrafts(), "Deselecting must preserve uncaptured pose edits");
    e.selectBody(0);
    e.capture();
    require(e.clip.tracks[1].evaluate(5).position.x == 7, "Linear interpolation");
    require(e.clip.tracks[1].evaluate(-1).position.x == 2 && e.clip.tracks[1].evaluate(99).position.x == 12,
            "Endpoint holds");
    require(!e.retime(10, 0), "Key collision accepted");
    require(e.retime(10, 20), "Retime failed");
    require(!e.configure(20, 30), "Truncating keys accepted");
    require(e.configure(21, 60), "Timing rejected");
    e.undo();
    require(e.clip.frames == 300, "Undo timing");
    e.redo();
    require(e.clip.fps == 60, "Redo timing");
    e.seek(20);
    e.clip.apply(20, scene, camera);
    scene.spheres[0].centerRadius.x = 13;
    e.observe(scene, camera);
    scene.spheres[0].centerRadius.x = 14;
    e.observe(scene, camera);
    e.endGesture();
    e.undo();
    require(!e.hasDrafts(), "Gesture did not undo in one command");
    e.redo();
    require(e.hasDrafts(), "Redo draft");
    e.seek(1);
    require(!e.hasDrafts(), "Seek must discard draft");
    e.undo();
    require(e.hasDrafts() && e.frame == 20, "Discard undo must restore frame and pose");
    e.capture();
    require(e.clip.tracks[1].keys.size() == 2 && e.clip.tracks[1].evaluate(20).position.x == 14,
            "Replace key");
    e.remove();
    e.seek(0);
    e.remove();
    require(e.clip.tracks[1].keys.empty() && e.clip.tracks[1].evaluate(0).position.x == 2,
            "Delete last key base restore");
    CameraData a;
    a.position = {0, 0, 0};
    a.lookAt = {0, 0, -1};
    a.rotateView(179 * 3.141592653589793 / 180, 0);
    auto b = a;
    b.rotateView(2 * 3.141592653589793 / 180, 0);
    auto mid = interpolate(cameraPose(a), cameraPose(b), 0.5);
    CameraData result;
    applyCameraPose(result, mid);
    require((result.lookAt - result.position).z > 0.999, "Camera did not use shortest rotation path");
    for (int i = 0; i < 150; ++i)
        e.configure(300 + i, 30);
    int count = 0;
    while (e.canUndo())
    {
        e.undo();
        ++count;
    }
    require(count == 100, "Undo capacity");
    std::cout << "Animation evaluation, editing and history tests passed\n";
    return 0;
}

int runExportTests(const std::filesystem::path& directory)
{
    auto root = freshDirectory(directory, "export-tests-");
    ExportSpec spec;
    spec.width = 64;
    spec.height = 48;
    spec.frames = 6;
    spec.fps = 30;
    spec.destination = root / "fake.mp4";
    int begun = 0, reads = 0;
    auto fake = std::make_unique<FakeSink>();
    auto* inspect = fake.get();
    FrameExport job(spec,
                    {[&](int f)
                     {
                         require(f == begun++, "Skipped rendering frame");
                     },
                     []
                     {
                         return true;
                     },
                     [&]
                     {
                         ++reads;
                         return colorFrame(64, 48);
                     }},
                    std::move(fake));
    waitExport(job);
    require(begun == 6 && reads == 6 && inspect->received.size() == 6,
            "Backpressure lost frames or caused rerenders");
    auto cancelled = std::make_unique<FakeSink>();
    FrameExport stop(spec,
                     {[](int)
                      {
                      },
                      []
                      {
                          return false;
                      },
                      []
                      {
                          return std::vector<unsigned char>{};
                      }},
                     std::move(cancelled));
    stop.tick();
    stop.cancel();
    require(stop.done() && stop.progress().state == SinkState::Cancelled, "Cancel failed");
    auto failure = std::make_unique<FakeSink>();
    auto* fail = failure.get();
    FrameExport broken(spec,
                       {[](int)
                        {
                        },
                        []
                        {
                            return false;
                        },
                        []
                        {
                            return std::vector<unsigned char>{};
                        }},
                       std::move(failure));
    fail->p = {SinkState::Failed, 0, "test error"};
    broken.tick();
    require(broken.done(), "Sink failure not terminal");
    TimedFrame timing;
    timing.timeDenominator = 30;
    timing.index = 299;
    require(timing.boundary(10000000, 1) == 100000000, "Timestamp rounding drift");
    std::vector<Float4> linear = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {0.5f, 0.5f, 0, 1}};
    auto rgba = displayRgba(linear, 2, 2);
    require(rgba[2] > rgba[0] && rgba[8] > rgba[10], "Display RGBA row orientation");
    savePng(root / "original.png", 2, 2, linear);
    saveRgbaPng(root / "shared.png", 2, 2, rgba);
    std::ifstream x(root / "original.png", std::ios::binary), y(root / "shared.png", std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(x), {}) ==
                std::string(std::istreambuf_iterator<char>(y), {}),
            "PNG conversion changed");
    spec.format = ExportFormat::PngSequence;
    spec.destination = root / "frames";
    FrameExport png(spec,
                    {[](int)
                     {
                     },
                     []
                     {
                         return true;
                     },
                     []
                     {
                         return colorFrame(64, 48);
                     }},
                    makeDesktopFrameSink());
    waitExport(png);
    require(png.progress().written == 6 && std::filesystem::exists(spec.destination / "frame-000005.png"),
            "PNG sequence export failed");
    bool rejected = false;
    try
    {
        spec.validate();
    }
    catch (...)
    {
        rejected = true;
    }
    require(rejected, "Existing output accepted");
    spec.destination = root / "original.png" / "blocked";
    FrameExport blocked(spec,
                        {[](int)
                         {
                             throw std::runtime_error("Should fail before rendering");
                         },
                         []
                         {
                             return true;
                         },
                         []
                         {
                             return colorFrame(64, 48);
                         }},
                        makeDesktopFrameSink());
    for (int i = 0; i < 1000 && !blocked.done(); ++i)
    {
        blocked.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(blocked.progress().state == SinkState::Failed, "Unwritable destination not reported");
#ifdef _WIN32
    spec.format = ExportFormat::Mp4;
    spec.destination = root / "windows.mp4";
    FrameExport mp4(spec,
                    {[](int)
                     {
                     },
                     []
                     {
                         return true;
                     },
                     []
                     {
                         return colorFrame(64, 48);
                     }},
                    makeDesktopFrameSink());
    waitExport(mp4);
    verifyVideo(spec.destination, 64, 48, 6, 30);
    spec.destination = root / "cancelled.mp4";
    FrameExport cancelMp4(spec,
                          {[](int)
                           {
                           },
                           []
                           {
                               return false;
                           },
                           []
                           {
                               return colorFrame(64, 48);
                           }},
                          makeDesktopFrameSink());
    cancelMp4.cancel();
    waitExport(cancelMp4);
    require(!std::filesystem::exists(spec.destination), "Cancelled MP4 was published");
#endif
    std::cout << "Export queue, timing, PNG, failure and cancellation tests passed: " << root << '\n';
    return 0;
}

int runExportGpuTests(const std::filesystem::path& assets, const std::filesystem::path& directory)
{
    auto root = freshDirectory(directory, "export-gpu-");
    auto scene = defaultScene(assets);
    CameraData camera;
    AnimationEditor animation(scene, camera);
    animation.capture();
    animation.seek(1);
    camera.moveLocal({1, 0, 0}, 0.1);
    animation.observe(scene, camera);
    animation.capture();
    auto original = cameraPose(camera);
    RenderSettings settings;
    settings.width = 32;
    settings.height = 24;
    settings.samples = 1;
    GpuRenderer renderer(assets);
    renderer.uploadScene(scene);
    std::vector<std::vector<unsigned char>> first;
    for (int run = 0; run < 2; ++run)
    {
        ExportSpec spec;
        spec.format = ExportFormat::PngSequence;
        spec.destination = root / std::to_string(run);
        spec.width = 32;
        spec.height = 24;
        spec.frames = 2;
        int index = 0;
        FrameExport job(spec,
                        {[&](int f)
                         {
                             index = f;
                             animation.clip.apply(f, scene, camera);
                             renderer.updateGeometry(scene);
                             renderer.reset(settings, camera);
                         },
                         [&]
                         {
                             renderer.poll();
                             if (!renderer.progress().finished)
                                 renderer.dispatch();
                             return renderer.progress().finished;
                         },
                         [&]
                         {
                             auto bytes = displayRgba(renderer.readback(), 32, 24);
                             if (!run)
                                 first.push_back(bytes);
                             else
                                 require(bytes == first.at(index), "GPU frames are not deterministic");
                             return bytes;
                         }},
                        makeDesktopFrameSink());
        waitExport(job);
    }
    animation.clip.apply(1, scene, camera);
    require(samePose(original, cameraPose(camera)), "Animation restore changed camera");
    std::cout << "Repeated GPU frame exports matched: " << root << '\n';
    return 0;
}
} // namespace rt
