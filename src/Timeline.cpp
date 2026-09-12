#include "Timeline.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rt
{
    Timeline::Timeline(const SceneData &scene, const CameraData &camera) : editor(scene, camera) {}

    Timeline::~Timeline() = default;

    void Timeline::prepareExport(const RenderSettings &committed)
    {
        requestExport = false;
        width = std::max(2, committed.width - committed.width % 2);
        height = std::max(2, committed.height - committed.height % 2);
        samples = committed.samples;
        auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const char* videos = SDL_GetUserFolder(SDL_FOLDER_VIDEOS);
        destination = ((videos ? std::filesystem::u8path(videos) : std::filesystem::current_path()) /
                       ("animation-" + std::to_string(stamp) + ".mp4"))
                          .u8string();
        format = 1;
        message.clear();
    }

    void Timeline::chooseDestination(SDL_Window* window)
    {
        if (dialogPending)
            return;

        message.clear();
        dialogResult = std::make_shared<DialogResult>();
        dialogPending = true;
        // SDL may invoke this callback on another thread, even after Timeline is destroyed.
        // Transfer a shared result holder to the callback instead of capturing this.
        auto* context = new std::shared_ptr<DialogResult>(dialogResult);
        auto callback = [](void* userdata, const char* const* files, int)
        {
            std::unique_ptr<std::shared_ptr<DialogResult>> owner(
                static_cast<std::shared_ptr<DialogResult>*>(userdata));
            auto &result = **owner;
            std::lock_guard<std::mutex> lock(result.mutex);

            if (!files)
                result.error = SDL_GetError();
            else if (files[0])
                result.path = files[0];
            result.ready = true;
        };

        if (format == 1)
        {
            static const SDL_DialogFileFilter filters[] = {{"MP4 video", "mp4"}};
            SDL_ShowSaveFileDialog(callback, context, window, filters, 1, destination.c_str());
        }
        else
        {
            auto parent = std::filesystem::u8path(destination).parent_path().u8string();
            SDL_ShowOpenFolderDialog(callback, context, window, parent.c_str(), false);
        }
    }

    void Timeline::receiveDestination()
    {
        if (!dialogPending)
            return;

        std::string path, error;
        {
            std::lock_guard<std::mutex> lock(dialogResult->mutex);

            if (!dialogResult->ready)
                return;

            path = dialogResult->path;
            error = dialogResult->error;
        }

        dialogPending = false;
        refreshAfterDialog = true;
        dialogResult.reset();

        if (!error.empty() || path.empty())
        {
            message = error.empty() ? "Export cancelled; no files were created."
                                    : "Cannot open export dialog: " + error;
            reopenExport = true;

            return;
        }

        auto chosen = std::filesystem::u8path(path);

        if (format == 0)
            chosen /= std::filesystem::u8path(destination).stem();
        else if (chosen.extension().empty())
            chosen += ".mp4";
        destination = chosen.u8string();
        actions.push_back({Export});
    }

    void Timeline::draw(SettingsPanel &panel, const RenderSettings &committed)
    {
        actions.clear();
        receiveDestination();
        panel.setHistoryAvailability(editor.canUndo(), editor.canRedo());

        if (panel.selectionRevision() != lastSelectionRevision)
        {
            if (panel.isCameraSelected())
            {
                editor.selectedTrack = 0;
                editor.selectedKey = -1;
            }
            else
                editor.selectBody(panel.selectedBodyIndex());
            lastSelectionRevision = panel.selectionRevision();
            draggingKey = -1;
        }
        else if (panel.selectedBodyIndex() != editor.selectedBody() ||
                 panel.isCameraSelected() != (editor.selectedTrack == 0))
        {
            if (editor.selectedTrack == 0)
                panel.selectCamera();
            else
                panel.selectBody(editor.selectedBody());
            lastSelectionRevision = panel.selectionRevision();
        }

        for (auto command : panel.takeCommands())
        {
            if (command == WorkspaceCommand::Escape)
            {
                if (job)
                    actions.push_back({Cancel});
                else if (currentMode == AnimationMode::Playback)
                    actions.push_back({Play});
                else if (!busy())
                {
                    actions.push_back({Select, -1});
                    panel.selectBody(-1);
                    lastSelectionRevision = panel.selectionRevision();
                }
            }
            else if (!busy())
            {
                if (command == WorkspaceCommand::Undo)
                    actions.push_back({Undo});
                if (command == WorkspaceCommand::Redo)
                    actions.push_back({Redo});
                if (command == WorkspaceCommand::Deselect)
                    actions.push_back({Select, -1});
                if (command == WorkspaceCommand::ExportAnimation)
                {
                    requestExport = true;
                    panel.showTimeline();
                }
            }
        }

        const bool typingGlobal = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();

        if (!busy() && !typingGlobal)
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
                actions.push_back({Undo});
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                actions.push_back({Redo});
            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && editor.selectedKey >= 0)
                actions.push_back({Remove});
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
                        std::min(job->frame() + 1, exportClip.frames), exportClip.frames, p.written);
            ImGui::ProgressBar(float(p.written) / exportClip.frames);

            if (ImGui::Button("Cancel export"))
                actions.push_back({Cancel});
            ImGui::End();
        }

        if (!message.empty())
            panel.setStatus(message);
        if (!panel.timelineVisible())
            return;

        if (requestExport || reopenExport)
            ImGui::SetNextWindowFocus();
        if (!ImGui::Begin("Timeline", nullptr, panel.layoutLocked() ? ImGuiWindowFlags_NoMove : 0))
        {
            ImGui::End();

            return;
        }

        const bool exporting = currentMode == AnimationMode::Export || dialogPending;
        const bool playing = currentMode == AnimationMode::Playback;
        bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        bool typing = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
        auto continueRow = [](float nextWidth)
        {
            float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= right)
                ImGui::SameLine();
        };
        auto buttonWidth = [](const char* label)
        {
            return ImGui::CalcTextSize(label).x + 2 * ImGui::GetStyle().FramePadding.x;
        };
        bool newRow = true;
        auto button = [&](const char* label, Command c, int v = 0, int other = 0)
        {
            if (!newRow)
                continueRow(buttonWidth(label));
            newRow = false;

            if (ImGui::Button(label))
                actions.push_back({c, v, other});
        };
        const auto* track = editor.hasSelection() ? &editor.clip.tracks[editor.selectedTrack] : nullptr;
        int previous = 0, next = editor.clip.frames - 1;

        if (track)
            for (const auto &key : track->keys)
            {
                if (key.frame < editor.frame)
                    previous = key.frame;
                if (key.frame > editor.frame)
                {
                    next = key.frame;
                    break;
                }
            }
        // Transport and key editing occupy separate rows; timing settings live in a popup.
        ImGui::BeginDisabled(exporting);
        button("|<", Seek, previous);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Previous keyframe");
        button("<", Seek, editor.frame - 1);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Previous frame");
        button(playing ? "Pause" : "Play", Play);
        button(">", Seek, editor.frame + 1);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Next frame");
        button(">|", Seek, next);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Next keyframe");
        continueRow(80 * ImGui::GetStyle().FontScaleDpi);
        ImGui::SetNextItemWidth(80 * ImGui::GetStyle().FontScaleDpi);
        int frame = editor.frame;

        if (ImGui::InputInt("##Frame", &frame, 0, 0))
            actions.push_back({Seek, frame});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Current frame (0 to %d)", editor.clip.frames - 1);
        continueRow(150 * ImGui::GetStyle().FontScaleDpi);
        ImGui::TextDisabled("/ %d   %.2f s", editor.clip.frames - 1, double(editor.frame) / editor.clip.fps);
        ImGui::EndDisabled();
        continueRow(buttonWidth("Animation settings"));
        ImGui::BeginDisabled(busy());

        if (ImGui::Button("Animation settings"))
        {
            draftFrames = editor.clip.frames;
            draftFps = editor.clip.fps;
            ImGui::OpenPopup("Animation settings");
        }

        if (ImGui::BeginPopup("Animation settings"))
        {
            ImGui::SetNextItemWidth(140);
            ImGui::InputInt("Frames", &draftFrames);
            ImGui::SetNextItemWidth(140);
            ImGui::InputInt("FPS", &draftFps);
            ImGui::TextDisabled("%.2f seconds", double(std::max(1, draftFrames)) / std::max(1, draftFps));

            if (ImGui::Button("Apply"))
            {
                actions.push_back({Configure, draftFrames, draftFps});
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::EndDisabled();
        ImGui::Separator();
        newRow = true;
        ImGui::BeginDisabled(busy());
        ImGui::BeginDisabled(!track);
        std::string capture =
            track ? (track->keyAt(editor.frame) < 0 ? "Add " : "Update ") + track->name + " keyframe"
                  : "Add keyframe";
        button(capture.c_str(), Capture);
        ImGui::BeginDisabled(!track || (editor.selectedKey < 0 && track->keyAt(editor.frame) < 0));
        button("Remove", Remove);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        continueRow(buttonWidth("Export..."));
        bool exportClicked = ImGui::Button("Export...");

        if (exportClicked || requestExport)
        {
            prepareExport(committed);
            ImGui::OpenPopup("Export animation");
        }

        if (editor.hasDrafts())
        {
            continueRow(ImGui::CalcTextSize("Pose not captured").x);
            ImGui::TextColored({1, 0.77f, 0.38f, 1}, "Pose not captured");

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Capture the edited track to keep its pose. Seeking, playback or export "
                                  "discards uncaptured poses. Revert is undoable.");
            continueRow(buttonWidth("Revert poses"));

            if (ImGui::SmallButton("Revert poses"))
                actions.push_back({Seek, editor.frame});
        }

        ImGui::EndDisabled();
        continueRow(110 + ImGui::CalcTextSize("Zoom").x);
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Zoom", &pixelsPerFrame, 2, 40, "%.0f");

        if (!exporting && !typing && focused && ImGui::IsKeyPressed(ImGuiKey_Space, false))
            actions.push_back({Play});
        if (ImGui::BeginChild("Track lanes", ImVec2(0, 0), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            const float labelWidth = std::max(110.f, ImGui::CalcTextSize("Black hole").x + 24),
                        row = ImGui::GetTextLineHeight() + 10;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            auto* dl = ImGui::GetWindowDrawList();
            float rulerWidth = std::max(1, editor.clip.frames - 1) * pixelsPerFrame + 20;
            ImGui::InvisibleButton("Ruler", ImVec2(labelWidth + rulerWidth, row));
            auto mouseFrame = [&]
            {
                return std::clamp(
                    int(std::lround((ImGui::GetIO().MousePos.x - origin.x - labelWidth) / pixelsPerFrame)), 0,
                    editor.clip.frames - 1);
            };

            if (!exporting && ImGui::IsItemActive())
                actions.push_back({Seek, mouseFrame()});
            int spacing = std::max(1, int(60 / pixelsPerFrame));
            // Only draw visible ticks, even for very long timelines.
            int first = std::max(0, int((ImGui::GetWindowPos().x - origin.x - labelWidth) / pixelsPerFrame));
            int last =
                std::min(editor.clip.frames,
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
                const auto &t = editor.clip.tracks[i];
                ImGui::PushID(i);
                ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + row * (i + 1)));

                if (ImGui::Selectable(t.name.c_str(), editor.selectedTrack == i, 0,
                                      ImVec2(labelWidth, row)) &&
                    !exporting)
                    actions.push_back({Select, i});
                ImGui::SameLine();
                ImGui::SetCursorScreenPos(ImVec2(origin.x + labelWidth, origin.y + row * (i + 1)));
                ImGui::InvisibleButton("Lane", ImVec2(rulerWidth, row));
                float y = origin.y + row * (i + 1.5f);
                dl->AddLine(ImVec2(origin.x + labelWidth, y), ImVec2(origin.x + labelWidth + rulerWidth, y),
                            IM_COL32(70, 70, 70, 255));
                bool hit = false;

                for (const auto &k : t.keys)
                {
                    float x = origin.x + labelWidth + k.frame * pixelsPerFrame;
                    auto color = editor.selectedTrack == i && editor.selectedKey == k.frame
                                     ? IM_COL32(255, 200, 60, 255)
                                     : IM_COL32(90, 180, 240, 255);
                    dl->AddQuadFilled(ImVec2(x, y - 6), ImVec2(x + 6, y), ImVec2(x, y + 6), ImVec2(x - 6, y),
                                      color);
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

                if (!exporting && !hit && draggingKey < 0 && ImGui::IsItemClicked())
                    actions.push_back({Select, -1});
                ImGui::PopID();
            }

            if (!exporting && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0))
                actions.push_back({Select, -1});
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
                                IM_COL32(255, 200, 60, 255), 2);
                }

                if (ImGui::IsMouseReleased(0))
                {
                    if (keyMoved)
                        actions.push_back({Retime, draggingKey, dragDestination});
                    draggingKey = -1;
                }
            }

            float x = origin.x + labelWidth + editor.frame * pixelsPerFrame;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + row * (editor.clip.tracks.size() + 1)),
                        IM_COL32(245, 80, 80, 255), 2);
        }

        ImGui::EndChild();

        if (reopenExport)
        {
            ImGui::OpenPopup("Export animation");
            reopenExport = false;
        }

        if (ImGui::BeginPopupModal("Export animation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::Combo("Format", &format, "PNG sequence\0MP4 (H.264)\0"))
            {
                std::filesystem::path path = std::filesystem::u8path(destination);
                path.replace_extension(format == 1 ? ".mp4" : "");
                destination = path.u8string();
            }

            ImGui::TextUnformatted(
                format == 1 ? "Choose a filename and location in the Save dialog."
                            : "Choose a parent folder; frames will be saved in a new animation subfolder.");
            if (!message.empty())
                ImGui::TextWrapped("%s", message.c_str());
            ImGui::InputInt("Width", &width);
            ImGui::InputInt("Height", &height);
            ImGui::InputInt("Samples per pixel", &samples);

            if (format == 1)
                ImGui::InputFloat("Bitrate (Mbps)", &bitrate, 1, 5, "%.1f");
            ImGui::Text("%d frames at %d FPS. Uses committed render settings and seed.", editor.clip.frames,
                        editor.clip.fps);
            std::string settingsError;

            try
            {
                exportSpec(committed).validateSettings();
            }
            catch (const std::exception &error)
            {
                settingsError = error.what();
            }

            if (!settingsError.empty())
                ImGui::TextColored({1, 0.55f, 0.45f, 1}, "%s", settingsError.c_str());
            ImGui::BeginDisabled(!settingsError.empty());

            if (ImGui::Button("Choose location and export..."))
            {
                ImGui::CloseCurrentPopup();
                chooseDestination(SDL_GL_GetCurrentWindow());
            }

            ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();

        for (const auto &a : actions)
            if (a.command == Select)
            {
                int body = a.value >= 0 ? editor.clip.tracks[a.value].body : -1;

                if (a.value == 0)
                    panel.selectCamera();
                else
                    panel.selectBody(body);
                lastSelectionRevision = panel.selectionRevision();
            }
    }

    ExportSpec Timeline::exportSpec(const RenderSettings &committed) const
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
        auto settings = committed;
        settings.width = width;
        settings.height = height;
        settings.samples = samples;
        settings.validate();

        return spec;
    }

    void Timeline::startExport(SceneData &scene, CameraData &camera, GpuRenderer &renderer,
                               const RenderSettings &committed)
    {
        auto spec = exportSpec(committed);
        spec.validate();
        exportSettings = committed;
        exportSettings.width = width;
        exportSettings.height = height;
        exportSettings.samples = samples;
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

    bool Timeline::update(SceneData &scene, CameraData &camera, GpuRenderer &renderer,
                          const RenderSettings &committed, RenderSettings &active, bool gesture)
    {
        bool changed = refreshAfterDialog;
        refreshAfterDialog = false;

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

                    if (a.value < 0)
                        draggingKey = -1;
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
            catch (const std::exception &e)
            {
                message = e.what();

                if (a.command == Export)
                    reopenExport = true;
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
            catch (const std::exception &e)
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
