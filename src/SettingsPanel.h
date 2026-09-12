#pragma once
#include "GpuRenderer.h"
#include "Viewport.h"
#include <SDL3/SDL.h>
#include <string>

namespace rt
{
enum class WorkspaceCommand
{
    Undo,
    Redo,
    Deselect,
    ExportAnimation,
    Escape
};

// UI-owned draft values and actions keep widgets independent of renderer lifetime.
struct PanelActions
{
    bool renderChanged = false;
    bool save = false;
    bool positionChanged = false;
    bool resetCamera = false;
    bool quit = false;
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
    void selectCamera();

    bool isCameraSelected() const
    {
        return cameraSelected;
    }

    void setDisplayImage(DisplayImage image)
    {
        displayedImage = image;
    }

    const ViewportRect& viewport() const
    {
        return imageRect;
    }

    const ViewportRect& viewportArea() const
    {
        return contentRect;
    }

    std::array<int, 2> viewportPixels() const;

    bool timelineVisible() const
    {
        return visible && panels[4];
    }

    bool layoutLocked() const
    {
        return lockedLayout;
    }

    void showTimeline()
    {
        panels[4] = true;
    }

    std::vector<WorkspaceCommand> takeCommands();

    void setHistoryAvailability(bool undo, bool redo)
    {
        canUndo = undo;
        canRedo = redo;
    }

    void setBodyAnchors(const std::vector<Vec3>& anchors)
    {
        bodyAnchors = anchors;
    }

    int selectedBodyIndex() const
    {
        return selectedBody;
    }

    std::uint64_t selectionRevision() const
    {
        return bodySelectionRevision;
    }

    bool isVisible() const
    {
        return visible;
    }

    void setEditingEnabled(bool enabled)
    {
        editingEnabled = enabled;
        if (!enabled)
        {
            dragAxis = -1;
            bodyEditPending = false;
        }
    }

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
    void drawGizmo(const PanelActions&, const CameraData&, const SceneData&, double aspect);
    void drawWorkspace(PanelActions&);
    void drawCamera(PanelActions&, double&, const CameraData&, const SceneData&);
    void drawRenderSettings(PanelActions&);
    void drawViewport(const PanelActions&, const CameraData&, const SceneData&, double aspect);
    void saveWorkspace();
    void processGizmoEvent(const SDL_Event&);
    std::vector<Vec3> bodyAnchors;
    int selectedBody = -1;
    bool cameraSelected = false;
    std::uint64_t bodySelectionRevision = 0;
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
    SDL_Window* window = nullptr;
    bool panels[6] = {
        true, true, true, true, true, true}; // Scene, Inspector, Camera, Render, Timeline, Viewport
    bool lockedLayout = true, resetLayout = false, showGizmos = true;
    bool controlsOpen = false, aboutOpen = false, canUndo = false, canRedo = false;
    bool viewportHovered = false, viewportFocused = false, saveRequested = false;
    unsigned int dockId = 0;
    DisplayImage displayedImage;
    ViewportRect contentRect, imageRect;
    std::string layoutFile, preferencesFile;
    std::vector<WorkspaceCommand> commands;
};
} // namespace rt
