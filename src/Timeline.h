#pragma once
#include "Animation.h"
#include "FrameExport.h"
#include "SettingsPanel.h"
#include <chrono>

namespace rt
{
enum class AnimationMode
{
    Editing,
    Playback,
    Export
};

class Timeline
{
  public:
    Timeline(const SceneData&, const CameraData&);
    ~Timeline();

    AnimationMode mode() const
    {
        return currentMode;
    }

    bool busy() const
    {
        return currentMode != AnimationMode::Editing;
    }

    void draw(SettingsPanel&, const RenderSettings& committed);
    // Call after applying all viewport edits, before ordinary preview scheduling.
    bool update(
        SceneData&, CameraData&, GpuRenderer&, const RenderSettings&, RenderSettings& active, bool gesture);

  private:
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
    int returnFrame = 0, lastBody = -2;
    bool frameStarted = false;
    std::chrono::steady_clock::time_point frameStart;
    float pixelsPerFrame = 8;
    int draggingKey = -1, dragDestination = -1;
    bool keyMoved = false;
    int draftFrames = 300, draftFps = 30;
    char destination[1024]{};
    int format = 1, width = 1920, height = 1080, samples = 30;
    float bitrate = 20;
    std::string message;
    void startExport(SceneData&, CameraData&, GpuRenderer&, const RenderSettings&);
};
} // namespace rt
