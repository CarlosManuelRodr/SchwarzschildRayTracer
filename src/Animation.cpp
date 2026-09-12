#include "Animation.h"
#include <algorithm>
#include <stdexcept>

namespace rt
{
    namespace
    {
        Quaternion unit(Quaternion q)
        {
            const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (n < 1e-15 || !std::isfinite(n))
                throw std::runtime_error("Invalid camera orientation");

            return {q.x / n, q.y / n, q.z / n, q.w / n};
        }

        Vec3 rotate(Quaternion q, Vec3 v)
        {
            const Vec3 u(q.x, q.y, q.z);
            return v + 2 * cross(u, cross(u, v) + q.w * v);
        }
    }

    Pose cameraPose(const CameraData& camera)
    {
        camera.basis(1);
        const Vec3 z = normalized(camera.position - camera.lookAt);
        Vec3 x = normalized(cross(camera.up, z)), y = cross(z, x);
        const double m[3][3] = {{x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z}};
        Quaternion q;
        const double trace = m[0][0] + m[1][1] + m[2][2];
        if (trace > 0)
        {
            const double s = 2 * std::sqrt(trace + 1);
            q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4};
        }
        else
        {
            int i = m[1][1] > m[0][0] ? 1 : 0;
            if (m[2][2] > m[i][i])
                i = 2;
            int j = (i + 1) % 3, k = (i + 2) % 3;
            const double s = 2 * std::sqrt(1 + m[i][i] - m[j][j] - m[k][k]);
            double v[3]{};
            v[i] = s / 4;
            v[j] = (m[j][i] + m[i][j]) / s;
            v[k] = (m[k][i] + m[i][k]) / s;
            q = {v[0], v[1], v[2], (m[k][j] - m[j][k]) / s};
        }
        return {camera.position, unit(q)};
    }

    void applyCameraPose(CameraData& camera, const Pose& pose)
    {
        const auto q = unit(pose.orientation);
        camera.position = pose.position;
        camera.lookAt = pose.position + rotate(q, {0, 0, -1});
        camera.up = rotate(q, {0, 1, 0});
    }

    Pose interpolate(const Pose& a, const Pose& b, const double t)
    {
        auto p = unit(a.orientation), q = unit(b.orientation);
        double d = p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w;
        if (d < 0)
        {
            q = {-q.x, -q.y, -q.z, -q.w};
            d = -d;
        }
        double u = 1 - t, v = t;
        if (d < 0.9995)
        {
            const double theta = std::acos(std::clamp(d, -1.0, 1.0));
            u = std::sin((1 - t) * theta) / std::sin(theta);
            v = std::sin(t * theta) / std::sin(theta);
        }
        return {a.position * (1 - t) + b.position * t,
                unit({u * p.x + v * q.x, u * p.y + v * q.y, u * p.z + v * q.z, u * p.w + v * q.w})};
    }

    bool samePose(const Pose& a, const Pose& b)
    {
        const auto d = a.position - b.position;
        auto p = a.orientation, q = b.orientation;
        const double c = std::abs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w);
        return dot(d, d) < 1e-20 && std::abs(1 - c) < 1e-12;
    }

    int AnimationTrack::keyAt(const int frame) const
    {
        const auto it = std::lower_bound(keys.begin(),
                                   keys.end(),
                                   frame,
                                   [](const Keyframe& k, const int f)
                                   {
                                       return k.frame < f;
                                   });
        return it != keys.end() && it->frame == frame ? static_cast<int>(it - keys.begin()) : -1;
    }

    Pose AnimationTrack::evaluate(const int frame) const
    {
        if (keys.empty())
            return base;

        const auto it = std::upper_bound(keys.begin(),
                                   keys.end(),
                                   frame,
                                   [](const int f, const Keyframe& k)
                                   {
                                       return f < k.frame;
                                   });
        if (it == keys.begin())
            return it->pose;

        if (it == keys.end())
            return keys.back().pose;

        const auto& a = *(it - 1);
        return interpolate(a.pose, it->pose, static_cast<double>(frame - a.frame) / (it->frame - a.frame));
    }

    std::vector<Pose> AnimationClip::evaluate(const int frame) const
    {
        std::vector<Pose> poses;
        poses.reserve(tracks.size());
        for (const auto& t : tracks)
            poses.push_back(t.evaluate(frame));
        return poses;
    }

    void AnimationClip::apply(const int frame, SceneData& scene, CameraData& camera, const bool drafts) const
    {
        for (const auto& t : tracks)
        {
            Pose p = drafts && t.draft ? *t.draft : t.evaluate(frame);
            if (t.body < 0)
                applyCameraPose(camera, p);
            else
            {
                auto& c = scene.spheres.at(t.body).centerRadius;
                c.x = static_cast<float>(p.position.x);
                c.y = static_cast<float>(p.position.y);
                c.z = static_cast<float>(p.position.z);
            }
        }
    }

    AnimationEditor::AnimationEditor(const SceneData& scene, const CameraData& camera)
    {
        clip.tracks.push_back({-1, "Camera", cameraPose(camera), {}, {}});
        for (int i = 0; i < static_cast<int>(scene.spheres.size()); ++i)
        {
            const auto& s = scene.spheres[i];
            const int kind = scene.materials[s.material.x].kindTexture.x;
            if (kind == Environment)
                continue;

            const std::string name = kind == Earth           ? "Earth"
                                     : kind == Moon          ? "Moon"
                                     : kind == Schwarzschild ? "Black hole"
                                     : kind == DiffuseLight  ? "Sun"
                                                             : "Body";
            clip.tracks.push_back(
                {i, name, {{s.centerRadius.x, s.centerRadius.y, s.centerRadius.z}, {}}, {}, {}}
                );
        }
    }

    AnimationEditor::State AnimationEditor::state() const
    {
        return {clip, frame, selectedTrack, selectedKey};
    }

    void AnimationEditor::restore(const State& s)
    {
        clip = s.clip;
        frame = s.frame;
        selectedTrack = s.track;
        selectedKey = s.key;
        gesture = false;
    }

    void AnimationEditor::checkpoint()
    {
        if (past.size() == 100)
            past.erase(past.begin());
        past.push_back(state());
        future.clear();
    }

    void AnimationEditor::endGesture()
    {
        gesture = false;
    }

    void AnimationEditor::observe(const SceneData& scene, const CameraData& camera)
    {
        for (auto& t : clip.tracks)
        {
            Pose current;
            if (t.body < 0)
                current = cameraPose(camera);
            else
            {
                auto c = scene.spheres.at(t.body).centerRadius;
                current.position = {c.x, c.y, c.z};
            }
            Pose expected = t.draft ? *t.draft : t.evaluate(frame);

            // Body geometry is stored as float; compare against its actual stored representation.
            if (t.body >= 0)
                expected.position = {
                    static_cast<float>(expected.position.x), static_cast<float>(expected.position.y), static_cast<float>(expected.position.z)};
            if (samePose(current, expected))
                continue;

            if (!gesture)
            {
                checkpoint();
                gesture = true;
            }

            if (t.keys.empty())
                t.base = current;
            else
                t.draft = current;
        }
    }

    bool AnimationEditor::hasDrafts() const
    {
        return std::any_of(clip.tracks.begin(),
                           clip.tracks.end(),
                           [](const auto& t)
                           {
                               return t.draft.has_value();
                           });
    }

    void AnimationEditor::seek(const int destination)
    {
        endGesture();
        if (hasDrafts())
        {
            checkpoint();
            for (auto& t : clip.tracks)
                t.draft.reset();
        }
        frame = std::clamp(destination, 0, clip.frames - 1);
        selectedKey = -1;
    }

    void AnimationEditor::capture()
    {
        if (!hasSelection())
            return;

        endGesture();
        checkpoint();
        auto& t = clip.tracks.at(selectedTrack);
        const Pose p = t.draft ? *t.draft : t.evaluate(frame);

        if (const int k = t.keyAt(frame); k >= 0)
            t.keys[k].pose = p;
        else
        {
            t.keys.push_back({frame, p});
            std::sort(t.keys.begin(),
                      t.keys.end(),
                      [](auto& a, auto& b)
                      {
                          return a.frame < b.frame;
                      });
        }

        t.draft.reset();
        selectedKey = frame;
    }

    void AnimationEditor::remove()
    {
        if (!hasSelection())
            return;
        endGesture();
        auto& t = clip.tracks.at(selectedTrack);
        const int k = t.keyAt(selectedKey >= 0 ? selectedKey : frame);
        if (k < 0)
            return;
        checkpoint();
        t.keys.erase(t.keys.begin() + k);
        t.draft.reset();
        selectedKey = -1;
    }

    bool AnimationEditor::retime(const int from, const int to)
    {
        if (!hasSelection())
            return false;

        endGesture();
        auto& t = clip.tracks.at(selectedTrack);
        const int k = t.keyAt(from);

        if (k < 0 || to < 0 || to >= clip.frames || (to != from && t.keyAt(to) >= 0))
            return false;
        if (from == to)
            return true;

        checkpoint();
        for (auto& track : clip.tracks)
            track.draft.reset();

        t.keys[k].frame = to;
        std::sort(t.keys.begin(),
                  t.keys.end(),
                  [](auto& a, auto& b)
                  {
                      return a.frame < b.frame;
                  });
        frame = to;
        selectedKey = to;
        return true;
    }

    bool AnimationEditor::configure(const int frames, const int fps)
    {
        if (frames < 1 || frames > 1000000 || fps < 1 || fps > 240)
            return false;

        for (const auto& t : clip.tracks)
            if (!t.keys.empty() && t.keys.back().frame >= frames)
                return false;

        if (frames == clip.frames && fps == clip.fps)
            return true;

        endGesture();
        checkpoint();
        clip.frames = frames;
        clip.fps = fps;
        frame = std::min(frame, frames - 1);
        return true;
    }

    void AnimationEditor::undo()
    {
        endGesture();
        if (past.empty())
            return;
        future.push_back(state());
        const auto s = past.back();
        past.pop_back();
        restore(s);
    }

    void AnimationEditor::redo()
    {
        endGesture();
        if (future.empty())
            return;
        past.push_back(state());
        const auto s = future.back();
        future.pop_back();
        restore(s);
    }

    void AnimationEditor::selectBody(const int body)
    {
        selectedTrack = -1;
        selectedKey = -1;
        if (body < 0)
            return; // A background click must not implicitly select the camera.
        for (int i = 0; i < static_cast<int>(clip.tracks.size()); ++i)
        {
            if (clip.tracks[i].body == body)
            {
                selectedTrack = i;
                selectedKey = -1;
                return;
            }
        }
    }
}
