#include "Timeline.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rt
{
Timeline::Timeline(const SceneData& scene, const CameraData& camera) : editor(scene, camera)
{
}

Timeline::~Timeline() = default;

void Timeline::draw(SettingsPanel& panel, const RenderSettings& committed)
{
    actions.clear();
    if (panel.selectedBodyIndex() != lastBody)
    {
        lastBody = panel.selectedBodyIndex();
        if (lastBody >= 0)
            editor.selectBody(lastBody);
    }
    else if (panel.selectedBodyIndex() != editor.clip.tracks[editor.selectedTrack].body)
    {
        lastBody = editor.clip.tracks[editor.selectedTrack].body;
        panel.selectBody(lastBody);
    }
    if (!panel.isVisible())
        return;
    if (job)
    {
        ImGui::Begin("Export progress");
        auto p = job->progress();
        ImGui::Text("%s | frame %d / %d | written %d",
                    p.state == SinkState::Starting    ? "Preparing encoder"
                    : p.state == SinkState::Finishing ? "Finalizing"
                                                      : "Rendering",
                    std::min(job->frame() + 1, exportClip.frames),
                    exportClip.frames,
                    p.written);
        ImGui::ProgressBar(float(p.written) / exportClip.frames);
        if (ImGui::Button("Cancel export"))
            actions.push_back({Cancel});
        ImGui::End();
    }
    if (!message.empty())
        panel.setStatus(message);
    const auto display = ImGui::GetIO().DisplaySize;
    float panelHeight =
        std::min(display.y * 0.6f,
                 6 * ImGui::GetFrameHeightWithSpacing() +
                     float(editor.clip.tracks.size() + 1) * (ImGui::GetFrameHeight() + 6) + 40);
    ImGui::SetNextWindowPos(ImVec2(12, std::max(12.f, display.y - panelHeight - 12)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::max(360.f, display.x - 24), panelHeight), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Timeline"))
    {
        ImGui::End();
        return;
    }
    const bool exporting = currentMode == AnimationMode::Export;
    const bool playing = currentMode == AnimationMode::Playback;
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool typing = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
    auto button = [&](const char* label, Command c, int v = 0, int other = 0)
    {
        if (ImGui::Button(label))
            actions.push_back({c, v, other});
        ImGui::SameLine();
    };
    ImGui::BeginDisabled(exporting);
    button(playing ? "Pause" : "Play", Play);
    button("< Frame", Seek, editor.frame - 1);
    button("Frame >", Seek, editor.frame + 1);
    auto& track = editor.clip.tracks[editor.selectedTrack];
    int previous = 0, next = editor.clip.frames - 1;
    for (const auto& key : track.keys)
    {
        if (key.frame < editor.frame)
            previous = key.frame;
        if (key.frame > editor.frame)
        {
            next = key.frame;
            break;
        }
    }
    button("< Key", Seek, previous);
    button("Key >", Seek, next);
    ImGui::SetNextItemWidth(160);
    int frame = editor.frame;
    if (ImGui::InputInt("Frame", &frame))
        actions.push_back({Seek, frame});
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy());
    button(track.keyAt(editor.frame) < 0 ? "Add Keyframe" : "Update Keyframe", Capture);
    button("Remove Keyframe", Remove);
    ImGui::BeginDisabled(!editor.canUndo());
    button("Undo", Undo);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!editor.canRedo());
    button("Redo", Redo);
    ImGui::EndDisabled();
    if (ImGui::Button("Export..."))
    {
        width = committed.width;
        height = committed.height;
        samples = committed.samples;
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::snprintf(
            destination, sizeof(destination), "Output/animation-%lld.mp4", static_cast<long long>(stamp));
        format = 1;
        ImGui::OpenPopup("Export animation");
    }
    ImGui::EndDisabled();
    ImGui::Text("%d / %d frames | %.3f s | %.3f s total%s",
                editor.frame,
                editor.clip.frames - 1,
                double(editor.frame) / editor.clip.fps,
                double(editor.clip.frames) / editor.clip.fps,
                editor.hasDrafts() ? " | Unkeyed changes (discarded on seek/play/export)" : "");
    ImGui::BeginDisabled(busy());
    ImGui::SetNextItemWidth(160);
    ImGui::InputInt("Frames", &draftFrames);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("FPS", &draftFps);
    ImGui::SameLine();
    button("Apply timing", Configure, draftFrames, draftFps);
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(130);
    ImGui::SliderFloat("Zoom", &pixelsPerFrame, 2, 40, "%.1f px/frame");
    if (!exporting && !typing)
    {
        if (focused && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            actions.push_back({Play});
        if (!playing)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && editor.selectedKey >= 0)
                actions.push_back({Remove});
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                actions.push_back({Undo});
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                actions.push_back({Redo});
        }
    }
    if (ImGui::BeginChild(
            "Track lanes", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const float labelWidth = std::max(110.f, ImGui::CalcTextSize("Black hole").x + 24),
                    row = ImGui::GetFrameHeight() + 6;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        auto* dl = ImGui::GetWindowDrawList();
        float rulerWidth = std::max(1, editor.clip.frames - 1) * pixelsPerFrame + 20;
        ImGui::InvisibleButton("Ruler", ImVec2(labelWidth + rulerWidth, row));
        auto mouseFrame = [&]
        {
            return std::clamp(
                int(std::lround((ImGui::GetIO().MousePos.x - origin.x - labelWidth) / pixelsPerFrame)),
                0,
                editor.clip.frames - 1);
        };
        if (!exporting && ImGui::IsItemActive())
            actions.push_back({Seek, mouseFrame()});
        int spacing = std::max(1, int(60 / pixelsPerFrame));
        // Only draw visible ticks, even for very long timelines.
        int first = std::max(0, int((ImGui::GetWindowPos().x - origin.x - labelWidth) / pixelsPerFrame));
        int last = std::min(editor.clip.frames,
                            int((ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - origin.x - labelWidth) /
                                pixelsPerFrame) +
                                1);
        for (int f = first / spacing * spacing; f < last; f += spacing)
        {
            float x = origin.x + labelWidth + f * pixelsPerFrame;
            char text[24];
            std::snprintf(text, sizeof(text), "%d", f);
            dl->AddText(ImVec2(x, origin.y), IM_COL32(190, 190, 190, 255), text);
        }
        for (int i = 0; i < int(editor.clip.tracks.size()); ++i)
        {
            const auto& t = editor.clip.tracks[i];
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + row * (i + 1)));
            if (ImGui::Selectable(t.name.c_str(), editor.selectedTrack == i, 0, ImVec2(labelWidth, row)) &&
                !exporting)
                actions.push_back({Select, i});
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(origin.x + labelWidth, origin.y + row * (i + 1)));
            ImGui::InvisibleButton("Lane", ImVec2(rulerWidth, row));
            float y = origin.y + row * (i + 1.5f);
            dl->AddLine(ImVec2(origin.x + labelWidth, y),
                        ImVec2(origin.x + labelWidth + rulerWidth, y),
                        IM_COL32(70, 70, 70, 255));
            bool hit = false;
            for (const auto& k : t.keys)
            {
                float x = origin.x + labelWidth + k.frame * pixelsPerFrame;
                auto color = editor.selectedTrack == i && editor.selectedKey == k.frame
                                 ? IM_COL32(255, 200, 60, 255)
                                 : IM_COL32(90, 180, 240, 255);
                dl->AddQuadFilled(
                    ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6), ImVec2(x - 6, y), color);
                if (!exporting && ImGui::IsItemHovered() && std::abs(ImGui::GetIO().MousePos.x - x) < 8 &&
                    ImGui::IsMouseClicked(0))
                {
                    actions.push_back({Select, i});
                    actions.push_back({Seek, k.frame, 1});
                    if (!playing)
                    {
                        draggingKey = k.frame;
                        dragDestination = k.frame;
                        keyMoved = false;
                    }
                    hit = true;
                }
            }
            if (!exporting && !hit && draggingKey < 0 && ImGui::IsItemActive())
            {
                actions.push_back({Select, i});
                actions.push_back({Seek, mouseFrame()});
            }
            ImGui::PopID();
        }
        if (draggingKey >= 0)
        {
            if (ImGui::IsMouseDragging(0, 4))
            {
                dragDestination = mouseFrame();
                keyMoved = true;
            }
            if (keyMoved)
            {
                float x = origin.x + labelWidth + dragDestination * pixelsPerFrame;
                dl->AddLine(ImVec2(x, origin.y),
                            ImVec2(x, origin.y + row * (editor.clip.tracks.size() + 1)),
                            IM_COL32(255, 200, 60, 255),
                            2);
            }
            if (ImGui::IsMouseReleased(0))
            {
                if (keyMoved)
                    actions.push_back({Retime, draggingKey, dragDestination});
                draggingKey = -1;
            }
        }
        float x = origin.x + labelWidth + editor.frame * pixelsPerFrame;
        dl->AddLine(ImVec2(x, origin.y),
                    ImVec2(x, origin.y + row * (editor.clip.tracks.size() + 1)),
                    IM_COL32(245, 80, 80, 255),
                    2);
    }
    ImGui::EndChild();
    if (ImGui::BeginPopupModal("Export animation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::Combo("Format", &format, "PNG sequence\0MP4 (H.264)\0"))
        {
            std::filesystem::path path = std::filesystem::u8path(destination);
            path.replace_extension(format == 1 ? ".mp4" : "");
            std::snprintf(destination, sizeof(destination), "%s", path.u8string().c_str());
        }
        ImGui::InputText(format == 1 ? "Output file" : "New output folder", destination, sizeof(destination));
        ImGui::InputInt("Width", &width);
        ImGui::InputInt("Height", &height);
        ImGui::InputInt("Samples per pixel", &samples);
        if (format == 1)
            ImGui::InputFloat("Bitrate (Mbps)", &bitrate, 1, 5, "%.1f");
        ImGui::Text("%d frames at %d FPS. Uses committed render settings and seed.",
                    editor.clip.frames,
                    editor.clip.fps);
        if (ImGui::Button("Start export"))
        {
            actions.push_back({Export});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
    for (const auto& a : actions)
        if (a.command == Select)
        {
            int body = editor.clip.tracks[a.value].body;
            panel.selectBody(body);
            lastBody = body;
        }
}

void Timeline::startExport(SceneData& scene,
                           CameraData& camera,
                           GpuRenderer& renderer,
                           const RenderSettings& committed)
{
    ExportSpec spec;
    spec.format = format == 1 ? ExportFormat::Mp4 : ExportFormat::PngSequence;
    spec.destination = std::filesystem::absolute(std::filesystem::u8path(destination));
    spec.width = width;
    spec.height = height;
    spec.frames = editor.clip.frames;
    spec.fps = editor.clip.fps;
    if (!std::isfinite(bitrate) || bitrate < 0.1f || bitrate > 200)
        throw std::runtime_error("Bitrate must be 0.1 to 200 Mbps");
    spec.bitrate = int(bitrate * 1000000);
    spec.validate();
    exportSettings = committed;
    exportSettings.width = width;
    exportSettings.height = height;
    exportSettings.samples = samples;
    exportSettings.validate();
    auto size = renderer.fitResolution(width, height);
    if (size[0] != width || size[1] != height)
        throw std::runtime_error("Export resolution exceeds GPU budget");
    editor.seek(editor.frame);
    exportClip = editor.clip;
    returnFrame = editor.frame;
    exportClip.apply(returnFrame, scene, camera);
    ExportRenderer callbacks;
    callbacks.beginFrame = [this, &scene, &camera, &renderer](int f)
    {
        exportClip.apply(f, scene, camera);
        renderer.updateGeometry(scene);
        renderer.reset(exportSettings, camera);
    };
    callbacks.completed = [&renderer]
    {
        return renderer.progress().finished;
    };
    callbacks.readFrame = [this, &renderer]
    {
        return displayRgba(renderer.readback(), width, height, exportSettings.exposure);
    };
    job = std::make_unique<FrameExport>(spec, std::move(callbacks), makeDesktopFrameSink());
    currentMode = AnimationMode::Export;
    message = "";
}

bool Timeline::update(SceneData& scene,
                      CameraData& camera,
                      GpuRenderer& renderer,
                      const RenderSettings& committed,
                      RenderSettings& active,
                      bool gesture)
{
    bool changed = false;
    if (!busy())
    {
        editor.observe(scene, camera);
        if (!gesture)
            editor.endGesture();
    }
    for (auto a : actions)
    {
        try
        {
            switch (a.command)
            {
            case Select:
                editor.selectedTrack = a.value;
                editor.selectedKey = -1;
                break;
            case Seek:
                currentMode = AnimationMode::Editing;
                editor.seek(a.value);
                if (a.other)
                    editor.selectedKey = editor.frame;
                changed = true;
                break;
            case Capture:
                editor.capture();
                changed = true;
                break;
            case Remove:
                editor.remove();
                changed = true;
                break;
            case Retime:
                if (!editor.retime(a.value, a.other))
                    message = "Cannot move a key onto an occupied frame.";
                changed = true;
                break;
            case Configure:
                if (!editor.configure(a.value, a.other))
                    message = "Use 1-1000000 frames / 1-240 FPS; move or remove keys before shortening.";
                draftFrames = editor.clip.frames;
                draftFps = editor.clip.fps;
                changed = true;
                break;
            case Undo:
                editor.undo();
                draftFrames = editor.clip.frames;
                draftFps = editor.clip.fps;
                changed = true;
                break;
            case Redo:
                editor.redo();
                draftFrames = editor.clip.frames;
                draftFps = editor.clip.fps;
                changed = true;
                break;
            case Play:
                if (currentMode == AnimationMode::Playback)
                {
                    currentMode = AnimationMode::Editing;
                    changed = true;
                }
                else
                {
                    editor.seek(editor.frame == editor.clip.frames - 1 ? 0 : editor.frame);
                    currentMode = AnimationMode::Playback;
                    frameStarted = false;
                }
                break;
            case Export:
                startExport(scene, camera, renderer, committed);
                break;
            case Cancel:
                if (job)
                    job->cancel();
                break;
            default:
                break;
            }
        }
        catch (const std::exception& e)
        {
            message = e.what();
            changed = true;
        }
    }
    actions.clear();
    if (changed && currentMode == AnimationMode::Editing)
        editor.clip.apply(editor.frame, scene, camera, true);
    if (currentMode == AnimationMode::Playback)
    {
        auto now = std::chrono::steady_clock::now();
        if (frameStarted && renderer.progress().finished &&
            std::chrono::duration<double>(now - frameStart).count() >= 1.0 / editor.clip.fps)
        {
            if (editor.frame + 1 == editor.clip.frames)
            {
                currentMode = AnimationMode::Editing;
                changed = true;
            }
            else
            {
                editor.seek(editor.frame + 1);
                frameStarted = false;
            }
        }
        if (currentMode == AnimationMode::Playback && !frameStarted)
        {
            editor.clip.apply(editor.frame, scene, camera);
            renderer.updateGeometry(scene);
            active = committed;
            active.width = std::max(1, active.width / 4);
            active.height = std::max(1, active.height / 4);
            active.samples = 1;
            renderer.reset(active, camera);
            frameStarted = true;
            frameStart = now;
        }
    }
    if (job)
    {
        active = exportSettings;
        try
        {
            job->tick();
        }
        catch (const std::exception& e)
        {
            message = e.what();
            job->cancel();
        }
        if (job->done())
        {
            auto p = job->progress();
            if (p.state == SinkState::Complete)
                message = std::string("Export complete: ") + destination;
            else if (p.state == SinkState::Failed)
                message = "Export failed: " + p.error;
            else if (message.empty())
                message = "Export cancelled. Completed PNG frames were retained; incomplete MP4 removed.";
            job.reset();
            currentMode = AnimationMode::Editing;
            editor.seek(returnFrame);
            editor.clip.apply(returnFrame, scene, camera);
            changed = true;
        }
    }
    return changed;
}
} // namespace rt
