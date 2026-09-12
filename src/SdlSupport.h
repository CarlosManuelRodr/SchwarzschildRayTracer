#pragma once
#include <SDL3/SDL.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace rt
{
    inline void checkSdl(const bool success, const char* operation)
    {
        if (!success)
            throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
    }

    using SdlSurface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

    class SdlVideo
    {
    public:
        SdlVideo()
        {
            checkSdl(SDL_Init(SDL_INIT_VIDEO), "Initialize SDL video");
        }

        ~SdlVideo()
        {
            SDL_Quit();
        }

        SdlVideo(const SdlVideo&) = delete;
        SdlVideo& operator=(const SdlVideo&) = delete;
    };

    // Keep this owner alive until all OpenGL resources have been destroyed.
    class SdlGlWindow
    {
    public:
        SdlGlWindow(const int width, const int height, const bool hidden = false)
        {
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4), "Set GL major version");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3), "Set GL minor version");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE),
                     "Set GL profile");
            checkSdl(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "Set double buffering");
            window.reset(
                SDL_CreateWindow("Schwarzschild GPU",
                                 width,
                                 height,
                                 SDL_WINDOW_OPENGL | (hidden ? SDL_WINDOW_HIDDEN
                                                             : (SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED))));
            checkSdl(bool(window), "Create OpenGL window");
            context = SDL_GL_CreateContext(window.get());
            checkSdl(context != nullptr, "Create OpenGL 4.3 context");
        }

        ~SdlGlWindow()
        {
            SDL_GL_DestroyContext(context);
        }

        SdlGlWindow(const SdlGlWindow&) = delete;
        SdlGlWindow& operator=(const SdlGlWindow&) = delete;

        [[nodiscard]] SDL_Window* get() const
        {
            return window.get();
        }

        private:
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
        SDL_GLContext context = nullptr;
    };
}
