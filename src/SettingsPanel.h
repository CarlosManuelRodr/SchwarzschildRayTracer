/**
 * @file
 * @brief Docked workspace controls, viewport presentation, and body transform editing.
 */
#pragma once
#include "GpuRenderer.h"
#include "Viewport.h"
#include <SDL3/SDL.h>
#include <string>

namespace rt
{
    /**
     * @brief Workspace actions queued for the application/timeline to consume.
     */
    enum class WorkspaceCommand
    {
        Undo,
        Redo,
        Deselect,
        ExportAnimation,
        Escape
    };

    /**
     * @brief Edits requested by widgets during the current frame.
     *
     * The application commits renderChanged from requestedSettings, applies camera
     * positionChanged/resetCamera, and applies bodyPosition when body is nonnegative.
     * Widgets do not mutate renderer resources directly.
     */
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

    /**
     * @brief Own ImGui workspace state and SDL/OpenGL UI backends.
     *
     * Use processEvent, beginFrame, draw, and render on the UI/context thread.
     * The SDL window and its current GL context must outlive this panel.
     */
    class SettingsPanel
    {
    public:
        /**
         * @brief Initialize the UI and draft settings for a borrowed application window.
         */
        SettingsPanel(SDL_Window* window, const RenderSettings &settings);

        /**
         * @brief Save workspace preferences and shut down ImGui backends.
         */
        ~SettingsPanel();
        SettingsPanel(const SettingsPanel &) = delete;
        SettingsPanel &operator=(const SettingsPanel &) = delete;

        /**
         * @brief Forward SDL input to ImGui and update any active transform gesture.
         */
        void processEvent(const SDL_Event &event);

        /**
         * @brief Whether mouse input belongs to a widget or gizmo rather than camera navigation.
         */
        bool capturesMouse() const;

        /**
         * @brief Whether a gizmo, text input, or an unfocused viewport blocks camera shortcuts.
         */
        bool capturesKeyboard() const;

        /**
         * @brief Start the next ImGui frame after event processing.
         */
        void beginFrame();

        /**
         * @brief Draw workspace panels and return the edits requested this frame.
         *
         * slowStep is the editable Shift-navigation distance; active describes the current
         * render, while camera and scene are the current editable state.
         */
        PanelActions draw(double &slowStep, const GpuProgress &progress, const RenderSettings &active,
                          bool preview, const CameraData &camera, const SceneData &scene);

        /**
         * @brief Submit ImGui draw data into the current OpenGL framebuffer.
         */
        void render();

        /**
         * @brief Select a stable sphere index for inspection, or clear selection with -1.
         */
        void selectBody(int body);

        /**
         * @brief Select the camera inspector and clear body selection.
         */
        void selectCamera();

        /**
         * @brief Whether the camera is the current inspector selection.
         */
        bool isCameraSelected() const
        {
            return cameraSelected;
        }

        /**
         * @brief Set a borrowed renderer-owned texture for this frame's viewport.
         */
        void setDisplayImage(DisplayImage image)
        {
            displayedImage = image;
        }

        /**
         * @brief Return the fitted image rectangle in logical window coordinates.
         */
        const ViewportRect &viewport() const
        {
            return imageRect;
        }

        /**
         * @brief Return the full viewport content rectangle before aspect-ratio fitting.
         */
        const ViewportRect &viewportArea() const
        {
            return contentRect;
        }

        /**
         * @brief Return available viewport dimensions in framebuffer pixels, accounting for DPI.
         */
        std::array<int, 2> viewportPixels() const;

        /**
         * @brief Whether both the workspace and timeline panel are visible.
         */
        bool timelineVisible() const
        {
            return visible && panels[4];
        }

        /**
         * @brief Whether dock rearrangement is disabled.
         */
        bool layoutLocked() const
        {
            return lockedLayout;
        }

        /**
         * @brief Enable the timeline panel in the workspace.
         */
        void showTimeline()
        {
            panels[4] = true;
        }

        /**
         * @brief Drain queued menu/shortcut commands for processing by the application.
         */
        std::vector<WorkspaceCommand> takeCommands();

        /**
         * @brief Update whether Undo and Redo actions are available.
         */
        void setHistoryAvailability(bool undo, bool redo)
        {
            canUndo = undo;
            canRedo = redo;
        }

        /**
         * @brief Copy lower-left image-space anchors (u,v,visible), indexed by sphere ID.
         */
        void setBodyAnchors(const std::vector<Vec3> &anchors)
        {
            bodyAnchors = anchors;
        }

        /**
         * @brief Return the selected sphere index, or -1 when no body is selected.
         */
        int selectedBodyIndex() const
        {
            return selectedBody;
        }

        /**
         * @brief Return the selection change counter used to synchronize the timeline.
         */
        std::uint64_t selectionRevision() const
        {
            return bodySelectionRevision;
        }

        /**
         * @brief Whether the workspace UI is visible.
         */
        bool isVisible() const
        {
            return visible;
        }

        /**
         * @brief Enable scene edits; disabling also cancels pending gizmo manipulation.
         */
        void setEditingEnabled(bool enabled)
        {
            editingEnabled = enabled;

            if (!enabled)
            {
                dragAxis = -1;
                bodyEditPending = false;
            }
        }

        /**
         * @brief Whether a transform axis or view-plane drag is active.
         */
        bool manipulatingBody() const
        {
            return dragAxis >= 0;
        }

        /**
         * @brief Synchronize requested render dimensions after viewport resizing.
         */
        void syncResolution(int width, int height);

        /**
         * @brief Show a status/error message, optionally clearing it when rendering finishes.
         */
        void setStatus(const std::string &message, bool error = false, bool untilRenderFinished = false);

        /**
         * @brief Return UI draft settings for validation and commitment by the application.
         */
        const RenderSettings &requestedSettings() const
        {
            return draft;
        }

        /**
         * @brief Whether render resolution follows the viewport instead of fixed dimensions.
         */
        bool followsWindow() const
        {
            return matchWindow;
        }

    private:
        void drawBodyEditor(PanelActions &, const CameraData &, const SceneData &, double aspect);
        void drawGizmo(const PanelActions &, const CameraData &, const SceneData &, double aspect);
        void drawWorkspace(PanelActions &);
        void drawCamera(PanelActions &, double &, const CameraData &, const SceneData &);
        void drawRenderSettings(PanelActions &);
        void drawViewport(const PanelActions &, const CameraData &, const SceneData &, double aspect);
        void saveWorkspace();
        void processGizmoEvent(const SDL_Event &);
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
        bool statusError = false, statusUntilRenderFinished = false;
        bool editingEnabled = true;
        SDL_Window* window = nullptr;
        bool panels[5] = {}; // Optional: Scene, Inspector, Camera, Render, Timeline
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
