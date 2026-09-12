#pragma once
#include "SceneData.h"
#include <optional>

namespace rt
{
struct Quaternion
{
    double x = 0, y = 0, z = 0, w = 1;
};

struct Pose
{
    Vec3 position;
    Quaternion orientation;
};

Pose cameraPose(const CameraData& camera);
void applyCameraPose(CameraData& camera, const Pose& pose);
Pose interpolate(const Pose& a, const Pose& b, double t);
bool samePose(const Pose& a, const Pose& b);

struct Keyframe
{
    int frame = 0;
    Pose pose;
};

struct AnimationTrack
{
    int body = -1; // -1 is the camera; other values are stable sphere indices.
    std::string name;
    Pose base;
    std::vector<Keyframe> keys;
    std::optional<Pose> draft;
    Pose evaluate(int frame) const;
    int keyAt(int frame) const;
};

struct AnimationClip
{
    int frames = 300, fps = 30;
    std::vector<AnimationTrack> tracks;
    std::vector<Pose> evaluate(int frame) const;
    void apply(int frame, SceneData& scene, CameraData& camera, bool drafts = false) const;
};

// Lightweight snapshots contain no textures or renderer state.
class AnimationEditor
{
  public:
    AnimationEditor(const SceneData& scene, const CameraData& camera);
    AnimationClip clip;
    int frame = 0, selectedTrack = 0, selectedKey = -1;
    void observe(const SceneData& scene, const CameraData& camera);
    void endGesture();
    void seek(int destination);
    void capture();
    void remove();
    bool retime(int from, int to);
    bool configure(int frames, int fps);
    void undo();
    void redo();

    bool canUndo() const
    {
        return !past.empty();
    }

    bool canRedo() const
    {
        return !future.empty();
    }

    void selectBody(int body);

    bool hasSelection() const
    {
        return selectedTrack >= 0 && selectedTrack < int(clip.tracks.size());
    }

    int selectedBody() const
    {
        return hasSelection() ? clip.tracks[selectedTrack].body : -1;
    }

    bool hasDrafts() const;

  private:
    struct State
    {
        AnimationClip clip;
        int frame, track, key;
    };

    std::vector<State> past, future;
    bool gesture = false;
    State state() const;
    void restore(const State& value);
    void checkpoint();
};

int runAnimationTests();
} // namespace rt
