#pragma once
#include "GpuRenderer.h"
#include <SDL3/SDL.h>
#include <string>

namespace rt
{
// UI-owned draft values and actions keep widgets independent of renderer lifetime.
struct PanelActions
{
    bool renderChanged = false;
    bool save = false;
};

class SettingsPanel
{
  public:
    SettingsPanel(SDL_Window* window, const RenderSettings& settings);
    ~SettingsPanel();
    SettingsPanel(const SettingsPanel&) = delete;
    SettingsPanel& operator=(const SettingsPanel&) = delete;

    void processEvent(const SDL_Event& event);
    bool capturesMouse() const;
    bool capturesKeyboard() const;
    void beginFrame();
    PanelActions draw(double& slowStep,
                      const GpuProgress& progress,
                      const RenderSettings& active,
                      bool preview);
    void render();
    void syncResolution(int width, int height);
    void setStatus(const std::string& message, bool error = false);

    const RenderSettings& requestedSettings() const
    {
        return draft;
    }

    bool followsWindow() const
    {
        return matchWindow;
    }

  private:
    RenderSettings draft;
    float uiScale = 1.0f;
    bool visible = true;
    bool matchWindow = true;
    std::string status;
    bool statusError = false;
};
} // namespace rt
