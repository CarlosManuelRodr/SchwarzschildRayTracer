#include "GpuRenderer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "SdlSupport.h"
#include "SettingsPanel.h"
#include "Timeline.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// Driver hints only; Windows/user graphics preferences still take precedence.
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace
{
std::filesystem::path executableDirectory()
{
    const char* path = SDL_GetBasePath();
    if (!path)
        throw std::runtime_error(std::string("Cannot resolve executable path: ") + SDL_GetError());
    return std::filesystem::u8path(path);
}

using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void benchmark(const std::filesystem::path& assets, rt::RenderSettings settings, bool compareCpu = true)
{
    rt::SdlGlWindow context(1, 1, true);
    auto scene = rt::defaultScene(assets);
    rt::CameraData camera;
    rt::GpuRenderer gpu(assets);
    gpu.uploadScene(scene);
    auto warm = settings;
    warm.width = 32;
    warm.height = 24;
    warm.samples = 1;
    gpu.reset(warm, camera);

    while (!gpu.progress().finished)
    {
        gpu.poll();
        gpu.dispatch();
        std::this_thread::yield();
    }

    gpu.reset(settings, camera);
    auto start = Clock::now();

    while (!gpu.progress().finished)
    {
        gpu.poll();
        gpu.dispatch();
        std::this_thread::yield();
    }

    auto image = gpu.readback();
    double gpuSeconds = seconds(start);
    std::cout << "GPU: " << gpu.device() << "\n"
              << settings.width << "x" << settings.height << ", " << settings.samples << " samples\n"
              << "GPU compute: " << gpu.progress().totalGpuMilliseconds / 1000
              << " s\nGPU completion including readback: " << gpuSeconds
              << " s\nLongest GPU batch: " << gpu.progress().maxBatchMilliseconds
              << " ms\nGPU invalid rays: " << gpu.progress().failures << std::endl;
    rt::savePng(executableDirectory() / "Output" / (compareCpu ? "benchmark-gpu.png" : "render-gpu.png"),
                settings.width,
                settings.height,
                image,
                settings.exposure);

    if (!compareCpu)
        return;
    start = Clock::now();
    std::uint64_t failures = 0;
    auto cpu = rt::renderCpu(scene, settings, camera, &failures);
    double cpuSeconds = seconds(start);
    double squared = 0;

    for (std::size_t i = 0; i < image.size(); ++i)
    {
        double x = image[i].x - cpu[i].x, y = image[i].y - cpu[i].y, z = image[i].z - cpu[i].z;
        squared += x * x + y * y + z * z;
    }

    rt::savePng(executableDirectory() / "Output" / "benchmark-cpu.png",
                settings.width,
                settings.height,
                cpu,
                settings.exposure);
    std::cout << "CPU reference (double precision, all cores): " << cpuSeconds
              << " s\nCompletion speedup: " << cpuSeconds / gpuSeconds
              << "x\nLinear RGB RMSE: " << std::sqrt(squared / (3 * image.size()))
              << "\nCPU invalid rays: " << failures << std::endl;

    if (failures || gpu.progress().failures)
        throw std::runtime_error("Benchmark encountered invalid rays");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        rt::RenderSettings settings;
        std::string mode;
        constexpr double normalMovementStep = 0.05;
        double slowMovementStep = 0.0005;
        auto assets = executableDirectory() / "assets";

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--test-cpu" || arg == "--test-gpu" || arg == "--benchmark" || arg == "--render" ||
                arg == "--test-animation" || arg == "--test-export" || arg == "--test-export-gpu")
                mode = arg;
            else if (arg == "--no-redshift")
                settings.redshift = false;
            else if (arg == "--full-scene-integration" || arg == "--adaptative")
            {
                auto requested = arg == "--adaptative" ? rt::RenderSettings::AdaptiveCutoff
                                                       : rt::RenderSettings::FullScene;
                if (settings.integrationMode != rt::RenderSettings::FixedRadius &&
                    settings.integrationMode != requested)
                    throw std::runtime_error(
                        "Choose either --full-scene-integration or --adaptative, not both");
                settings.integrationMode = requested;
            }
            else if (arg == "--exposure")
            {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for --exposure");
                std::size_t used = 0;
                settings.exposure = std::stof(argv[i], &used);
                if (used != std::string(argv[i]).size())
                    throw std::runtime_error("Invalid exposure");
            }
            else if (arg == "--slow-step")
            {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for --slow-step");
                std::size_t used = 0;
                slowMovementStep = std::stod(argv[i], &used);
                if (used != std::string(argv[i]).size() || !std::isfinite(slowMovementStep) ||
                    slowMovementStep <= 0 || slowMovementStep >= normalMovementStep)
                    throw std::runtime_error("--slow-step must be greater than 0 and less than 0.05");
            }
            else if (arg == "--width" || arg == "--height" || arg == "--samples" || arg == "--seed" ||
                     arg == "--assets")
            {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for " + arg);

                if (arg == "--assets")
                    assets = argv[i];
                else
                {
                    std::size_t used = 0;
                    int value = std::stoi(argv[i], &used);

                    if (used != std::string(argv[i]).size())
                        throw std::runtime_error("Invalid numeric argument");

                    if (arg == "--width")
                        settings.width = value;
                    else if (arg == "--height")
                        settings.height = value;
                    else if (arg == "--samples")
                        settings.samples = value;
                    else
                        settings.seed = std::uint32_t(value);
                }
            }
            else if (arg == "--help")
            {
                std::cout
                    << "SchwarzschildRayTracer [--width N --height N --samples N --seed N --assets DIR]\n"
                    << "  --test-cpu | --test-gpu | --benchmark | --render\n"
                    << "  --test-animation | --test-export | --test-export-gpu\n"
                    << "  --exposure N | --no-redshift\n"
                    << "  --slow-step N (Shift movement per tap; default 0.0005; held speed 20*N units/s)\n"
                    << "  --full-scene-integration | --adaptative (default: fixed radius)\n"
                    << "Left-drag looks; arrows/WASD move; Q/E rise/descend; P saves; F1 toggles settings; "
                       "Escape exits.\n";
                return 0;
            }
            else
                throw std::runtime_error("Unknown option: " + arg);
        }

        settings.validate();
        const char* integrationName =
            settings.integrationMode == rt::RenderSettings::FixedRadius ? "fixed radius"
            : settings.integrationMode == rt::RenderSettings::FullScene ? "full scene"
                                                                        : "adaptive cutoff";
        std::cout << "Integration: " << integrationName << std::endl;

        if (mode == "--test-cpu")
            return rt::runCpuTests();

        if (mode == "--test-animation")
            return rt::runAnimationTests();
        if (mode == "--test-export")
            return rt::runExportTests(executableDirectory() / "Output");
        rt::SdlVideo video;
        if (mode == "--test-export-gpu")
        {
            rt::SdlGlWindow context(1, 1, true);
            return rt::runExportGpuTests(assets, executableDirectory() / "Output");
        }

        if (mode == "--test-gpu")
        {
            rt::SdlGlWindow context(1, 1, true);

            return rt::runGpuTests(assets);
        }

        if (mode == "--benchmark" || mode == "--render")
        {
            benchmark(assets, settings, mode == "--benchmark");

            return 0;
        }

        auto scene = rt::defaultScene(assets);
        rt::CameraData camera;
        const rt::CameraData initialCamera = camera;
        rt::SdlGlWindow window(settings.width, settings.height);
        if (!SDL_GL_SetSwapInterval(1))
            std::cerr << "VSync unavailable: " << SDL_GetError() << '\n';

        // Renderer is destroyed before window, while its GL context is still valid.
        rt::GpuRenderer renderer(assets);
        renderer.uploadScene(scene);
        int drawableWidth = 0, drawableHeight = 0;
        rt::checkSdl(SDL_GetWindowSizeInPixels(window.get(), &drawableWidth, &drawableHeight),
                     "Get initial maximized size");
        auto initialSize = renderer.fitResolution(drawableWidth, drawableHeight);
        settings.width = initialSize[0];
        settings.height = initialSize[1];
        renderer.reset(settings, camera);
        rt::SettingsPanel panel(window.get(), settings);
        rt::Timeline timeline(scene, camera);
        std::cout
            << "GPU: " << renderer.device()
            << "\nLeft-drag to look; arrows/WASD move; Q/E rise/descend; P saves; F1 toggles settings.\n";
        bool running = true, minimized = false;
        bool focused = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
        bool dragging = false;
        SDL_FPoint mousePosition{}, clickPosition{};
        bool lookMoved = false;
        bool geometryDirty = false;
        constexpr double mouseSensitivity = 0.004;
        auto last = Clock::now(), titleTime = last;
        auto lastMovement = last;
        bool preview = false, cameraPending = false;
        auto activeSettings = settings;
        auto pendingSettings = settings;
        bool pendingMatchWindow = true;
        bool renderSettingsPending = false;
        bool matchWindow = true;
        auto lastSettingsEdit = last;
        auto saveImage = [&]
        {
            try
            {
                auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
                auto path = executableDirectory() / "Output" / ("img-" + std::to_string(stamp) + ".png");
                renderer.saveDisplayed(path);
                panel.setStatus("Saved " + path.u8string());
                std::cout << "Saved " << path << std::endl;
            }
            catch (const std::exception& error)
            {
                panel.setStatus(error.what(), true);
            }
        };

        while (running)
        {
            bool reset = false, tapped = false;
            panel.setEditingEnabled(!timeline.busy());
            SDL_Event event;

            while (SDL_PollEvent(&event))
            {
                panel.processEvent(event);
                const bool captureMouse = panel.capturesMouse() || timeline.busy();
                const bool captureKeyboard = panel.capturesKeyboard() || timeline.busy();
                if (captureMouse)
                    dragging = false;
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
                    (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE &&
                     !captureKeyboard))
                    running = false;

                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                {
                    focused = false;
                    dragging = false;
                }

                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    focused = true;

                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                    focused && !minimized && !captureMouse)
                {
                    dragging = true;
                    mousePosition = {event.button.x, event.button.y};
                    clickPosition = mousePosition;
                    lookMoved = false;
                }

                if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT &&
                    dragging && !lookMoved && !captureMouse && focused && !minimized)
                {
                    int width = 0, height = 0;
                    rt::checkSdl(SDL_GetWindowSize(window.get(), &width, &height), "Get picking viewport");
                    if (width > 0 && height > 0)
                        panel.selectBody(
                            renderer.pickDisplayed(event.button.x / width, 1.0 - event.button.y / height));
                }

                if ((event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) ||
                    event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
                    dragging = false;

                if (event.type == SDL_EVENT_MOUSE_MOTION && dragging && focused && !minimized &&
                    !captureMouse)
                {
                    SDL_FPoint position{event.motion.x, event.motion.y};
                    SDL_FPoint delta{position.x - mousePosition.x, position.y - mousePosition.y};
                    if (!lookMoved &&
                        std::hypot(position.x - clickPosition.x, position.y - clickPosition.y) >= 4)
                        lookMoved = true;
                    if (!lookMoved)
                        continue;
                    mousePosition = position;

                    if (delta.x != 0 || delta.y != 0)
                    {
                        camera.rotateView(delta.x * mouseSensitivity, -delta.y * mouseSensitivity);
                        reset = true;
                    }
                }

                // Handle quick taps even if the key is released between frame polls.
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && focused && !minimized &&
                    !captureKeyboard)
                {
                    rt::Vec3 movement;
                    switch (event.key.key)
                    {
                    case SDLK_RIGHT:
                    case SDLK_D:
                        movement.x = 1;
                        break;
                    case SDLK_LEFT:
                    case SDLK_A:
                        movement.x = -1;
                        break;
                    case SDLK_UP:
                    case SDLK_W:
                        movement.z = 1;
                        break;
                    case SDLK_DOWN:
                    case SDLK_S:
                        movement.z = -1;
                        break;
                    case SDLK_Q:
                        movement.y = 1;
                        break;
                    case SDLK_E:
                        movement.y = -1;
                        break;
                    default:
                        break;
                    }

                    if (rt::dot(movement, movement) > 0)
                    {
                        camera.moveLocal(movement,
                                         (event.key.mod & SDL_KMOD_SHIFT) ? slowMovementStep
                                                                          : normalMovementStep);
                        reset = true;
                        tapped = true;
                    }
                }

                if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                {
                    minimized = true;
                    dragging = false;
                }
                if (event.type == SDL_EVENT_WINDOW_RESTORED)
                {
                    minimized = false;
                }


                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                {
                    dragging = false;
                    minimized = (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) != 0 ||
                                event.window.data1 <= 0 || event.window.data2 <= 0;

                    if (!minimized && matchWindow && !timeline.busy())
                    {
                        auto size = renderer.fitResolution(int(event.window.data1), int(event.window.data2));
                        settings.width = size[0];
                        settings.height = size[1];
                        panel.syncResolution(settings.width, settings.height);
                        reset = true;
                    }
                }

                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P &&
                    !captureKeyboard && !minimized)
                    saveImage();
            }

            if (!running)
                break;
            auto now = Clock::now();
            double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
            last = now;

            if (minimized && !timeline.busy())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            panel.setBodyAnchors(renderer.bodyAnchors());
            panel.beginFrame();
            auto actions =
                panel.draw(slowMovementStep, renderer.progress(), activeSettings, preview, camera, scene);
            timeline.draw(panel, settings);
            if (actions.body >= 0 && actions.body < int(scene.spheres.size()))
            {
                auto& center = scene.spheres[actions.body].centerRadius;
                center.x = float(actions.bodyPosition.x);
                center.y = float(actions.bodyPosition.y);
                center.z = float(actions.bodyPosition.z);
                geometryDirty = true;
                reset = true;
            }
            if (actions.positionChanged)
            {
                camera.lookAt += actions.position - camera.position;
                camera.position = actions.position;
                reset = true;
            }
            if (actions.resetCamera)
            {
                camera = initialCamera;
                reset = true;
            }
            if (actions.save)
                saveImage();
            if (actions.renderChanged)
            {
                try
                {
                    auto requested = panel.requestedSettings();
                    if (panel.followsWindow())
                    {
                        int width = 0, height = 0;
                        rt::checkSdl(SDL_GetWindowSizeInPixels(window.get(), &width, &height),
                                     "Get drawable size");
                        auto size = renderer.fitResolution(width, height);
                        requested.width = size[0];
                        requested.height = size[1];
                    }
                    requested.validate();
                    const auto supported = renderer.fitResolution(requested.width, requested.height);
                    if (supported[0] != requested.width || supported[1] != requested.height)
                        throw std::runtime_error(
                            "Resolution exceeds GPU memory budget. Reduce width or height.");
                    pendingSettings = requested;
                    pendingMatchWindow = panel.followsWindow();
                    panel.syncResolution(requested.width, requested.height);
                    renderSettingsPending = true;
                    lastSettingsEdit = now;
                    panel.setStatus("Updating render...");
                }
                catch (const std::exception& error)
                {
                    renderSettingsPending = false;
                    panel.setStatus(error.what(), true);
                }
            }

            if (focused && !tapped && !panel.capturesKeyboard() && !timeline.busy())
            {
                auto key = [](SDL_Keycode k)
                {
                    return SDL_GetKeyboardState(nullptr)[SDL_GetScancodeFromKey(k, nullptr)];
                };
                rt::Vec3 movement((key(SDLK_RIGHT) || key(SDLK_D)) - (key(SDLK_LEFT) || key(SDLK_A)),
                                  key(SDLK_Q) - key(SDLK_E),
                                  (key(SDLK_UP) || key(SDLK_W)) - (key(SDLK_DOWN) || key(SDLK_S)));

                if (rt::dot(movement, movement) > 0)
                {
                    bool slow = key(SDLK_LSHIFT) || key(SDLK_RSHIFT);
                    double speed = slow ? slowMovementStep / normalMovementStep : 1.0;
                    camera.moveLocal(movement, dt * speed);
                    reset = true;
                }
            }

            renderer.poll();
            bool wasBusy = timeline.busy();
            bool animated =
                timeline.update(scene,
                                camera,
                                renderer,
                                settings,
                                activeSettings,
                                dragging || panel.manipulatingBody() || ImGui::IsAnyItemActive() || reset);
            if (animated)
            {
                geometryDirty = true;
                reset = true;
            }
            if (timeline.busy())
            {
                cameraPending = false;
                renderSettingsPending = false;
                geometryDirty = false;
                preview = timeline.mode() == rt::AnimationMode::Playback;
            }
            else if (wasBusy)
            {
                // Force a full-quality restore, even if the last preview was interrupted.
                preview = false;
                cameraPending = true;
            }
            if (reset)
            {
                cameraPending = true;
                lastMovement = now;
            }

            if (!timeline.busy())
            {
                if (renderSettingsPending && seconds(lastSettingsEdit) >= 0.15)
                {
                    settings = pendingSettings;
                    matchWindow = pendingMatchWindow;
                    preview = false;
                    activeSettings = settings;
                    if (geometryDirty)
                    {
                        renderer.updateGeometry(scene);
                        geometryDirty = false;
                    }
                    renderer.reset(activeSettings, camera);
                    cameraPending = false;
                    renderSettingsPending = false;
                    panel.setStatus("Rendering updated settings.");
                }

                // Finish each preview even if newer input arrives. Otherwise a held
                // key would continually cancel the slow rays and no preview would finish.
                bool settled = std::chrono::duration<double>(now - lastMovement).count() >= 0.15;
                bool canReplace = !preview || renderer.progress().finished;
                if (canReplace && (cameraPending || (preview && settled)))
                {
                    preview = !settled;
                    activeSettings = settings;
                    if (preview)
                    {
                        activeSettings.width = std::max(1, settings.width / 4);
                        activeSettings.height = std::max(1, settings.height / 4);
                        activeSettings.samples = 1;
                    }

                    if (geometryDirty)
                    {
                        renderer.updateGeometry(scene);
                        geometryDirty = false;
                    }
                    renderer.reset(activeSettings, camera);
                    cameraPending = false;
                }

            } // Ordinary progressive rendering is suspended during playback/export.

            if (!minimized)
            {
                int width = 0, height = 0;
                rt::checkSdl(SDL_GetWindowSizeInPixels(window.get(), &width, &height), "Get drawable size");
                renderer.present(width, height);
                panel.render();
                rt::checkSdl(SDL_GL_SwapWindow(window.get()), "Swap window");
            }

            else
                ImGui::EndFrame();

            renderer.dispatch();

            if (seconds(titleTime) > 0.25)
            {
                const auto& p = renderer.progress();
                std::ostringstream title;
                title << "Schwarzschild | "
                      << (settings.integrationMode == rt::RenderSettings::FixedRadius ? "fixed radius"
                          : settings.integrationMode == rt::RenderSettings::FullScene ? "full scene"
                                                                                      : "adaptive cutoff")
                      << " | " << std::fixed << std::setprecision(1) << p.meanSamples << "/"
                      << activeSettings.samples << " spp | " << activeSettings.width << "x"
                      << activeSettings.height << (preview ? " preview" : " refine") << " | GPU "
                      << p.lastBatchMilliseconds << " ms | invalid " << p.failures << " | "
                      << renderer.device();
                rt::checkSdl(SDL_SetWindowTitle(window.get(), title.str().c_str()), "Set window title");
                titleTime = Clock::now();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << std::endl;

        return 1;
    }
}
