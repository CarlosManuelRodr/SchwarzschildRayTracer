#include "GpuRenderer.h"
#include <SFML/Window.hpp>
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
#else
#include <unistd.h>
#endif

namespace
{
std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));

    if (!n || n == path.size())
        throw std::runtime_error("Cannot resolve executable path");
    path.resize(n);

    return std::filesystem::path(path).parent_path();
#else
    std::string path(4096, '\0');
    auto n = readlink("/proc/self/exe", path.data(), path.size());

    if (n <= 0 || std::size_t(n) == path.size())
        throw std::runtime_error("Cannot resolve executable path");
    path.resize(std::size_t(n));

    return std::filesystem::path(path).parent_path();
#endif
}

using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

sf::ContextSettings contextSettings()
{
    return sf::ContextSettings(0, 0, 0, 4, 3, sf::ContextSettings::Core);
}

void benchmark(const std::filesystem::path& assets, rt::RenderSettings settings, bool compareCpu = true)
{
    sf::Context context(contextSettings(), 1, 1);
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
        auto assets = executableDirectory() / "assets";

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];

            if (arg == "--test-cpu" || arg == "--test-gpu" || arg == "--benchmark" || arg == "--render")
                mode = arg;
            else if (arg == "--no-redshift")
                settings.redshift = false;
            else if (arg == "--exposure")
            {
                if (++i >= argc)
                    throw std::runtime_error("Missing value for --exposure");
                std::size_t used = 0;
                settings.exposure = std::stof(argv[i], &used);
                if (used != std::string(argv[i]).size())
                    throw std::runtime_error("Invalid exposure");
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
                    << "  --exposure N | --no-redshift\n"
                    << "Arrows/Q/E move; WASD aim; P saves current image; Escape exits.\n";
                return 0;
            }
            else
                throw std::runtime_error("Unknown option: " + arg);
        }

        settings.validate();

        if (mode == "--test-cpu")
            return rt::runCpuTests();

        if (mode == "--test-gpu")
        {
            sf::Context context(contextSettings(), 1, 1);

            return rt::runGpuTests(assets);
        }

        if (mode == "--benchmark" || mode == "--render")
        {
            benchmark(assets, settings, mode == "--benchmark");

            return 0;
        }

        auto scene = rt::defaultScene(assets);
        rt::CameraData camera;
        sf::Window window(sf::VideoMode(unsigned(settings.width), unsigned(settings.height)),
                          "Schwarzschild GPU",
                          sf::Style::Default,
                          contextSettings());
        window.setVerticalSyncEnabled(true);
        window.setKeyRepeatEnabled(false);

        // Renderer is destroyed before window, while its GL context is still valid.
        rt::GpuRenderer renderer(assets);
        renderer.uploadScene(scene);
        renderer.reset(settings, camera);
        std::cout << "GPU: " << renderer.device()
                  << "\nP saves the current image; arrows/Q/E move; WASD aim.\n";
        bool running = true, focused = true, minimized = false, redraw = true;
        auto last = Clock::now(), titleTime = last;

        while (running)
        {
            bool reset = false, tapped = false;
            sf::Event event;

            while (window.pollEvent(event))
            {
                if (event.type == sf::Event::Closed ||
                    (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape))
                    running = false;

                if (event.type == sf::Event::LostFocus)
                    focused = false;

                if (event.type == sf::Event::GainedFocus)
                    focused = true;

                // Handle quick taps even if the key is released between frame polls.
                if (event.type == sf::Event::KeyPressed)
                {
                    rt::Vec3 movement, aim;
                    switch (event.key.code)
                    {
                    case sf::Keyboard::Right:
                        movement.x = 1;
                        break;
                    case sf::Keyboard::Left:
                        movement.x = -1;
                        break;
                    case sf::Keyboard::Up:
                        movement.z = -1;
                        break;
                    case sf::Keyboard::Down:
                        movement.z = 1;
                        break;
                    case sf::Keyboard::Q:
                        movement.y = 1;
                        break;
                    case sf::Keyboard::E:
                        movement.y = -1;
                        break;
                    case sf::Keyboard::D:
                        aim.x = 1;
                        break;
                    case sf::Keyboard::A:
                        aim.x = -1;
                        break;
                    case sf::Keyboard::W:
                        aim.z = -1;
                        break;
                    case sf::Keyboard::S:
                        aim.z = 1;
                        break;
                    default:
                        break;
                    }

                    if (rt::dot(movement, movement) + rt::dot(aim, aim) > 0)
                    {
                        auto candidate = camera;
                        candidate.position += 0.05 * movement;
                        candidate.lookAt += 0.05 * (movement + aim);

                        try
                        {
                            candidate.basis(double(settings.width) / settings.height);
                            camera = candidate;
                            reset = true;
                            tapped = true;
                        }
                        catch (const std::exception&)
                        {
                        }
                    }
                }

                if (event.type == sf::Event::Resized)
                {
                    minimized = event.size.width == 0 || event.size.height == 0;

                    if (!minimized)
                    {
                        auto size = renderer.fitResolution(int(event.size.width), int(event.size.height));
                        settings.width = size[0];
                        settings.height = size[1];
                        reset = true;
                    }
                }

                if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::P)
                {
                    try
                    {
                        auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
                        auto path =
                            executableDirectory() / "Output" / ("img-" + std::to_string(stamp) + ".png");

                        if (!reset && !minimized)
                        {
                            rt::savePng(path,
                                        settings.width,
                                        settings.height,
                                        renderer.readback(),
                                        settings.exposure);
                            std::cout << "Saved " << path << std::endl;
                        }
                    }
                    catch (const std::exception& error)
                    {
                        std::cerr << "Export failed: " << error.what() << std::endl;
                    }
                }
            }

            if (!running)
                break;
            auto now = Clock::now();
            double dt = std::min(0.05, std::chrono::duration<double>(now - last).count());
            last = now;

            if (focused && !minimized && !tapped)
            {
                auto key = [](sf::Keyboard::Key k)
                {
                    return sf::Keyboard::isKeyPressed(k) ? 1.0 : 0.0;
                };
                rt::Vec3 movement(key(sf::Keyboard::Right) - key(sf::Keyboard::Left),
                                  key(sf::Keyboard::Q) - key(sf::Keyboard::E),
                                  key(sf::Keyboard::Down) - key(sf::Keyboard::Up));
                rt::Vec3 aim(key(sf::Keyboard::D) - key(sf::Keyboard::A),
                             0,
                             key(sf::Keyboard::S) - key(sf::Keyboard::W));

                if (rt::dot(movement, movement) + rt::dot(aim, aim) > 0)
                {
                    auto candidate = camera;
                    candidate.position += dt * movement;
                    candidate.lookAt += dt * (movement + aim);

                    try
                    {
                        candidate.basis(double(settings.width) / settings.height);
                        camera = candidate;
                        reset = true;
                    }
                    catch (const std::exception&)
                    {
                    }
                }
            }

            if (minimized)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (reset)
            {
                renderer.reset(settings, camera);
                redraw = true;
            }

            if (renderer.poll())
                redraw = true;

            if (redraw)
            {
                auto size = window.getSize();
                renderer.present(int(size.x), int(size.y));
                window.display();
                redraw = false;
            }

            renderer.dispatch();

            if (seconds(titleTime) > 0.25)
            {
                const auto& p = renderer.progress();
                std::ostringstream title;
                title << "Schwarzschild | " << std::fixed << std::setprecision(1) << p.meanSamples << "/"
                      << settings.samples << " spp | " << settings.width << "x" << settings.height
                      << " | GPU " << p.lastBatchMilliseconds << " ms | invalid " << p.failures << " | "
                      << renderer.device();
                window.setTitle(title.str());
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
