/**
 * @file
 * @brief Editable keyframes and pose interpolation, independent of GPU resources.
 */
#pragma once
#include "SceneData.h"
#include <optional>

namespace rt
{
    /**
     * @brief Rotation quaternion stored as (x,y,z,w), with identity (0,0,0,1).
     *
     * Pose operations normalize it; it maps local camera axes into world axes.
     */
    struct Quaternion
    {
        double x = 0, y = 0, z = 0, w = 1;
    };

    /**
     * @brief World position in horizon units and a camera orientation.
     *
     * Local camera forward is -Z and up is +Y; bodies currently use position only.
     */
    struct Pose
    {
        Vec3 position;
        Quaternion orientation;
    };

    /**
     * @brief Validate the camera and extract its position and unit orientation.
     */
    Pose cameraPose(const CameraData &camera);

    /**
     * @brief Set position, lookAt, and up from a pose; preserve the field of view.
     *
     * The new lookAt is one unit forward. Throws for a degenerate orientation.
     */
    void applyCameraPose(CameraData &camera, const Pose &pose);

    /**
     * @brief Interpolate position linearly and orientation along the shortest rotation.
     * @param t Interpolation fraction; use [0,1] to stay between the two poses.
     * Uses spherical quaternion interpolation, with a linear fallback for nearby rotations.
     */
    Pose interpolate(const Pose &a, const Pose &b, double t);

    /**
     * @brief Compare poses within position/rotation tolerances, treating q and -q as equal.
     *
     * Requires unit orientation quaternions.
     */
    bool samePose(const Pose &a, const Pose &b);

    /**
     * @brief Pose at a zero-based integer frame, independent of playback time.
     */
    struct Keyframe
    {
        int frame = 0;
        Pose pose;
    };

    /**
     * @brief Sorted, unique keyframes for the camera or one stable sphere index.
     *
     * base is used without keys; draft holds an uncommitted viewport edit.
     */
    struct AnimationTrack
    {
        int body = -1; // -1 is the camera; other values are stable sphere indices.
        std::string name;
        Pose base;
        std::vector<Keyframe> keys;
        std::optional<Pose> draft;

        /**
         * @brief Evaluate committed keys, holding endpoint poses outside their frame range.
         *
         * Returns base when no keys exist; does not include draft edits.
         */
        Pose evaluate(int frame) const;

        /**
         * @brief Return the key vector index at the exact frame, or -1 if none exists.
         */
        int keyAt(int frame) const;
    };

    /**
     * @brief Timeline duration, playback rate, and ordered pose tracks.
     *
     * Frames describe frozen scene snapshots, not physical orbits or emission-time history.
     */
    struct AnimationClip
    {
        int frames = 300, fps = 30;
        std::vector<AnimationTrack> tracks;

        /**
         * @brief Evaluate committed poses in track order at the requested frame.
         */
        std::vector<Pose> evaluate(int frame) const;

        /**
         * @brief Apply evaluated camera/body poses to a scene snapshot.
         * @param drafts Prefer pending draft poses when true; export uses committed keys only.
         * Sphere indices must still refer to the same bodies as when the clip was created.
         */
        void apply(int frame, SceneData &scene, CameraData &camera, bool drafts = false) const;
    };

    /**
     * @brief Manage keyframe edits and bounded undo history using lightweight pose snapshots.
     *
     * No textures or GPU resources are copied. A viewport gesture creates one undo checkpoint.
     */
    class AnimationEditor
    {
    public:
        /**
         * @brief Capture the initial camera and non-environment bodies as unkeyed tracks.
         */
        AnimationEditor(const SceneData &scene, const CameraData &camera);

        /**
         * @brief Editable clip. Keep each track's keys sorted with unique frame numbers.
         */
        AnimationClip clip;
        /**
         * @brief Current frame, selected track index, and selected key frame number (-1 if absent).
         */
        int frame = 0, selectedTrack = 0, selectedKey = -1;

        /**
         * @brief Capture viewport changes into unkeyed base poses or keyed-track drafts.
         */
        void observe(const SceneData &scene, const CameraData &camera);

        /**
         * @brief End the current edit gesture so the next edit gets a new undo checkpoint.
         */
        void endGesture();

        /**
         * @brief Clamp the playhead to the clip, discard drafts, and clear key selection.
         */
        void seek(int destination);

        /**
         * @brief Insert or replace a key at the playhead on the selected track, consuming its draft.
         */
        void capture();

        /**
         * @brief Remove the selected track's key at the selected key frame or current playhead.
         */
        void remove();

        /**
         * @brief Move the selected track's key from one frame to another.
         *
         * Returns false for an invalid frame, missing key, or occupied destination.
         */
        bool retime(int from, int to);

        /**
         * @brief Set duration/rate without removing keys; return false if keys would fall outside.
         *
         * Supported ranges are 1..1,000,000 frames and 1..240 frames per second.
         */
        bool configure(int frames, int fps);

        /**
         * @brief Restore the previous edit snapshot; do nothing when history is empty.
         */
        void undo();

        /**
         * @brief Restore the next undone snapshot; do nothing when redo history is empty.
         */
        void redo();

        /**
         * @brief Whether a previous edit snapshot is available.
         */
        bool canUndo() const
        {
            return !past.empty();
        }

        /**
         * @brief Whether an undone edit snapshot is available.
         */
        bool canRedo() const
        {
            return !future.empty();
        }

        /**
         * @brief Select the track for a sphere index; a negative index clears selection.
         *
         * Camera selection is handled separately by the timeline.
         */
        void selectBody(int body);

        /**
         * @brief Whether selectedTrack currently identifies an existing track.
         */
        bool hasSelection() const
        {
            return selectedTrack >= 0 && selectedTrack < int(clip.tracks.size());
        }

        /**
         * @brief Return the selected sphere index, or -1 for the camera or no selection.
         */
        int selectedBody() const
        {
            return hasSelection() ? clip.tracks[selectedTrack].body : -1;
        }

        /**
         * @brief Whether any track has an uncommitted viewport pose.
         */
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
        void restore(const State &value);
        void checkpoint();
    };

    /**
     * @brief Run pose interpolation, timeline editing, and navigation regression tests.
     */
    int runAnimationTests();
} // namespace rt
