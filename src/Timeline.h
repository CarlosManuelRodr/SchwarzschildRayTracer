/**
 * @file
 * @brief ImGui timeline, playback, and export coordination.
 */
#pragma once
#include "Animation.h"
#include "FrameExport.h"
#include "SettingsPanel.h"
#include <chrono>
#include <mutex>

namespace rt
{
    /**
     * @brief Mutually exclusive editing, progressive playback, and export modes.
     */
    enum class AnimationMode
    {
        Editing,
        Playback,
        Export
    };

    /**
     * @brief Connect the keyframe editor to viewport edits and progressive frame rendering.
     *
     * The application calls draw to collect actions and update to apply them. Playback
     * waits for a complete preview rather than showing partially finished ray batches.
     */
    class Timeline
    {
    public:
        /**
         * @brief Initialize tracks from the current scene and camera.
         */
        Timeline(const SceneData &, const CameraData &);

        /**
         * @brief End timeline ownership and cancel any unfinished export job.
         */
        ~Timeline();

        /**
         * @brief Return the current editing/playback/export mode.
         */
        AnimationMode mode() const
        {
            return currentMode;
        }

        /**
         * @brief Whether playback, export, or a pending destination dialog prevents ordinary edits.
         */
        bool busy() const
        {
            return currentMode != AnimationMode::Editing || dialogPending;
        }

        /**
         * @brief Draw timeline/export controls and queue actions using committed render settings.
         */
        void draw(SettingsPanel &, const RenderSettings &committed);

        /**
         * @brief Apply queued edits and advance playback/export after viewport input.
         *
         * Call before ordinary preview scheduling; active receives the settings being rendered.
         * The gesture flag groups continuous viewport movement into one undo operation.
         * Returns true when edits require the application to refresh ordinary scene rendering.
         * Playback/export ownership is reported separately through mode() and busy().
         */
        bool update(SceneData &, CameraData &, GpuRenderer &, const RenderSettings &, RenderSettings &active,
                    bool gesture);

    private:
        friend int runGpuTests(const std::filesystem::path &);
        void prepareExport(const RenderSettings &);
        ExportSpec exportSpec(const RenderSettings &) const;

        enum Command
        {
            None,
            Seek,
            Capture,
            Remove,
            Retime,
            Configure,
            Undo,
            Redo,
            Play,
            Select,
            Export,
            Cancel
        };

        struct Action
        {
            Command command = None;
            int value = 0, other = 0;
        };

        std::vector<Action> actions;
        AnimationEditor editor;
        AnimationMode currentMode = AnimationMode::Editing;
        std::unique_ptr<FrameExport> job;
        AnimationClip exportClip;
        RenderSettings exportSettings;
        int returnFrame = 0;
        std::uint64_t lastSelectionRevision = 0;
        bool frameStarted = false;
        std::chrono::steady_clock::time_point frameStart;
        float pixelsPerFrame = 8;
        int draggingKey = -1, dragDestination = -1;
        bool keyMoved = false;
        int draftFrames = 300, draftFps = 30;
        std::string destination;

        struct DialogResult
        {
            std::mutex mutex;
            bool ready = false;
            std::string path, error;
        };

        std::shared_ptr<DialogResult> dialogResult;
        bool dialogPending = false, reopenExport = false, refreshAfterDialog = false, requestExport = false;
        void chooseDestination(SDL_Window* window);
        void receiveDestination();
        int format = 1, width = 1920, height = 1080, samples = 30;
        float bitrate = 20;
        std::string message;
        void startExport(SceneData &, CameraData &, GpuRenderer &, const RenderSettings &);
    };
} // namespace rt
