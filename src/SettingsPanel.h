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
    bool positionChanged = false;
    bool resetCamera = false;
    Vec3 position;
    int body = -1;
    Vec3 bodyPosition;
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
                      bool preview,
                      const CameraData& camera,
                      const SceneData& scene);
    void render();
    void selectBody(int body);
    int selectedBodyIndex() const { return selectedBody; }
    bool isVisible() const { return visible; }
    void setEditingEnabled(bool enabled) { editingEnabled=enabled; if (!enabled) { dragAxis=-1; bodyEditPending=false; } }

    bool manipulatingBody() const
    {
        return dragAxis >= 0;
    }

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
    void drawBodyEditor(PanelActions&, const CameraData&, const SceneData&, double aspect);
    void processGizmoEvent(const SDL_Event&);
    int selectedBody = -1;
    int dragAxis = -1; // XYZ or 3 for translation in the view plane.
    bool editorOpen = false, gizmoVisible = false, bodyEditPending = false;
    SDL_FPoint gizmoOrigin{}, dragStart{};
    std::array<SDL_FPoint, 3> gizmoEnds{};
    Vec3 displayedBodyPosition, dragBodyPosition, pendingBodyPosition;
    Vec3 gizmoRight, gizmoUp, dragRight, dragUp;
    double gizmoLength = 1, dragLength = 1, gizmoPixelScale = 1, dragPixelScale = 1;
    SDL_FPoint dragScreenAxis{};
    std::vector<Vec3> originalBodyPositions;
    RenderSettings draft;
    float uiScale = 1.0f;
    bool visible = true;
    bool matchWindow = true;
    std::string status;
    bool statusError = false;
    bool editingEnabled = true;
};
} // namespace rt
