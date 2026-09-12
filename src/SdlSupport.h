/**
 * @file
 * @brief RAII ownership for SDL video, surfaces, and the OpenGL window.
 */
#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace rt
{
    /**
     * @brief Throw std::runtime_error with the operation and SDL error when success is false.
     */
    inline void checkSdl(const bool success, const char* operation)
    {
        if (!success)
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }

    /**
     * @brief Surface owner that releases pixels through SDL_DestroySurface.
     */
    using SdlSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    /**
     * @brief Own the application-wide SDL video lifetime; destroy after all SDL resources.
     */
    class SdlVideo
    {
    public:
        /**
         * @brief Initialize SDL video or throw with the SDL error.
         */
        SdlVideo()
        {
            checkSdl(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL video");
        }

        /**
         * @brief Shut down SDL after dependent windows and resources have been released.
         */
        ~SdlVideo()
        {
            SDL_Quit();
        }

        SdlVideo(const SdlVideo &) = delete;
        SdlVideo &operator=(const SdlVideo &) = delete;
    };

    /**
     * @brief Own an SDL window and OpenGL 4.3 core context.
     *
     * Keep this owner alive until all GL resources are destroyed. Visible windows start
     * maximized and resizable; hidden windows support offscreen validation.
     */
    class SdlGlWindow
    {
    public:
        /**
         * @brief Create the window and its GL context; throw on SDL initialization failures.
         */
        SdlGlWindow(const int width, const int height, const bool hidden = false)
        {
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4), "Set GL major version");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3), "Set GL minor version");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE),
                     "Set GL profile");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "Set double buffering");
            window.reset(SDL_CreateWindow(
                "Schwarzschild GPU", width, height,
                SDL_WINDOW_OPENGL |
                    (hidden ? SDL_WINDOW_HIDDEN : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED))));
            checkSdl(bool(window), "Create OpenGL window");
            context = SDL_GL_CreateContext(window.get());
            checkSdl(context != nullptr, "Create OpenGL 4.3 context");
        }

        /**
         * @brief Destroy the GL context before the owned SDL window.
         */
        ~SdlGlWindow()
        {
            SDL_GL_DestroyContext(context);
        }

        SdlGlWindow(const SdlGlWindow &) = delete;
        SdlGlWindow &operator=(const SdlGlWindow &) = delete;

        /**
         * @brief Return a borrowed SDL window pointer; ownership stays with this object.
         */
        [[nodiscard]] SDL_Window* get() const
        {
            return window.get();
        }

    private:
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
        SDL_GLContext context = nullptr;
    };
} // namespace rt
