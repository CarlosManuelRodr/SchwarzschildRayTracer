#include "SettingsPanel.h"
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace rt
{
    namespace
    {
        const char* panelNames[] = {"Scene", "Inspector", "Camera", "Render Settings", "Timeline"};

        const char* nameOf(const SceneData &scene, int body)
        {
            int kind = scene.materials[scene.spheres[body].material.x].kindTexture.x;

            return kind == Earth           ? "Earth"
                   : kind == Moon          ? "Moon"
                   : kind == Schwarzschild ? "Black hole"
                   : kind == DiffuseLight  ? "Sun"
                                           : "Body";
        }

        void tip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", text);
        }
    } // namespace

    SettingsPanel::SettingsPanel(SDL_Window* owner, const RenderSettings &settings)
        : draft(settings), window(owner)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.IniFilename = nullptr;
        // Hidden validation windows never load or modify the user's workspace.
        if (!(SDL_GetWindowFlags(owner) & SDL_WINDOW_HIDDEN))
        {
            if (char* pref = SDL_GetPrefPath("SchwarzschildRayTracer", "Workspace"))
            {
                auto root = std::filesystem::u8path(pref);
                SDL_free(pref);
                layoutFile = (root / "layout.ini").u8string();
                preferencesFile = (root / "panels.txt").u8string();
                io.IniFilename = layoutFile.c_str();
                std::ifstream input(std::filesystem::u8path(preferencesFile));
                int version = 0;
                input >> version;

                if (version == 1 || version == 2)
                {
                    for (bool &panel : panels)
                        input >> panel;
                    // Version 1 also stored a viewport visibility flag; it is now always visible.
                    if (version == 1)
                    {
                        bool oldViewport;
                        input >> oldViewport;
                    }

                    input >> lockedLayout >> showGizmos;
                }
            }
        }

        ImGui::StyleColorsDark();
        auto &style = ImGui::GetStyle();
        style.WindowPadding = {14, 12};
        style.FramePadding = {8, 5};
        style.ItemSpacing = {8, 8};
        style.WindowRounding = 4;
        style.FrameRounding = 3;
        style.TabRounding = 3;
        style.PopupRounding = 4;
        style.WindowBorderSize = 1;
        style.FrameBorderSize = 0;
        style.GrabMinSize = 10;
        auto* c = style.Colors;
        c[ImGuiCol_WindowBg] = {0.075f, 0.085f, 0.10f, 1};
        c[ImGuiCol_ChildBg] = {0.06f, 0.07f, 0.085f, 1};
        c[ImGuiCol_Text] = {0.88f, 0.90f, 0.94f, 1};
        c[ImGuiCol_TextDisabled] = {0.48f, 0.53f, 0.60f, 1};
        c[ImGuiCol_TitleBg] = c[ImGuiCol_MenuBarBg] = {0.09f, 0.10f, 0.12f, 1};
        c[ImGuiCol_TitleBgActive] = {0.12f, 0.14f, 0.17f, 1};
        c[ImGuiCol_Border] = {0.17f, 0.19f, 0.23f, 1};
        c[ImGuiCol_FrameBg] = {0.13f, 0.15f, 0.18f, 1};
        c[ImGuiCol_FrameBgHovered] = {0.19f, 0.23f, 0.28f, 1};
        c[ImGuiCol_Button] = {0.16f, 0.20f, 0.25f, 1};
        c[ImGuiCol_ButtonHovered] = {0.22f, 0.33f, 0.43f, 1};
        c[ImGuiCol_ButtonActive] = {0.19f, 0.40f, 0.57f, 1};
        c[ImGuiCol_Header] = {0.14f, 0.19f, 0.25f, 1};
        c[ImGuiCol_HeaderHovered] = {0.20f, 0.29f, 0.38f, 1};
        c[ImGuiCol_HeaderActive] = {0.20f, 0.36f, 0.49f, 1};
        c[ImGuiCol_Tab] = {0.10f, 0.12f, 0.15f, 1};
        c[ImGuiCol_TabSelected] = {0.18f, 0.25f, 0.32f, 1};
        c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = {0.35f, 0.68f, 0.88f, 1};
        uiScale = std::max(1.f, SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(owner)));
        style.ScaleAllSizes(uiScale);
        style.FontScaleDpi = uiScale;
        // Use an installed proportional font without adding a redistributed font dependency.
        std::vector<std::filesystem::path> fonts;
#ifdef _WIN32
        if (const char* windows = SDL_getenv("WINDIR"))
            fonts.push_back(std::filesystem::u8path(windows) / "Fonts" / "segoeui.ttf");
#else
        fonts = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                 "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};
#endif
        for (const auto &font : fonts)
            if (std::filesystem::exists(font))
            {
                io.Fonts->AddFontFromFileTTF(font.u8string().c_str(), 16);
                break;
            }
        if (!ImGui_ImplSDL3_InitForOpenGL(owner, SDL_GL_GetCurrentContext()))
            throw std::runtime_error("Cannot initialize ImGui SDL3 backend");

        if (!ImGui_ImplOpenGL3_Init("#version 430 core"))
            throw std::runtime_error("Cannot initialize ImGui OpenGL backend");
    }

    SettingsPanel::~SettingsPanel()
    {
        saveWorkspace();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    void SettingsPanel::saveWorkspace()
    {
        if (preferencesFile.empty())
            return;

        std::ofstream output(std::filesystem::u8path(preferencesFile));
        output << "2\n";

        for (bool panel : panels)
            output << panel << ' ';
        output << lockedLayout << ' ' << showGizmos << '\n';
    }

    void SettingsPanel::selectCamera()
    {
        selectBody(-1);
        cameraSelected = true;
    }

    std::vector<WorkspaceCommand> SettingsPanel::takeCommands()
    {
        auto result = std::move(commands);
        commands.clear();

        return result;
    }

    std::array<int, 2> SettingsPanel::viewportPixels() const
    {
        auto scale = ImGui::GetIO().DisplayFramebufferScale;

        return contentRect.pixels(scale.x, scale.y);
    }

    void SettingsPanel::processEvent(const SDL_Event &event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (editingEnabled)
            processGizmoEvent(event);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.key == SDLK_F1)
            {
                visible = !visible;
                dragAxis = -1;
                gizmoVisible = false;
                bodyEditPending = false;
            }

            if (event.key.key == SDLK_ESCAPE && !ImGui::GetIO().WantTextInput)
            {
                dragAxis = -1;
                bodyEditPending = false;
                commands.push_back(WorkspaceCommand::Escape);
            }

            if (event.key.key == SDLK_P && !ImGui::GetIO().WantTextInput && editingEnabled)
                saveRequested = true;
        }
    }

    bool SettingsPanel::capturesMouse() const
    {
        float x = 0, y = 0;
        SDL_GetMouseState(&x, &y);

        return manipulatingBody() || !imageRect.contains(x, y) || !viewportHovered;
    }

    bool SettingsPanel::capturesKeyboard() const
    {
        return manipulatingBody() || !viewportFocused || ImGui::GetIO().WantTextInput;
    }

    void SettingsPanel::beginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void SettingsPanel::drawWorkspace(PanelActions &actions)
    {
        float menuHeight = 0;

        if (visible && ImGui::BeginMainMenuBar())
        {
            menuHeight = ImGui::GetWindowHeight();

            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Image...", "P", false, editingEnabled))
                    actions.save = true;
                if (ImGui::MenuItem("Export Animation...", nullptr, false, editingEnabled))
                    commands.push_back(WorkspaceCommand::ExportAnimation);
                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                    actions.quit = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo pose / keyframe edit", "Ctrl+Z", false, editingEnabled && canUndo))
                    commands.push_back(WorkspaceCommand::Undo);
                if (ImGui::MenuItem("Redo pose / keyframe edit", "Ctrl+Y", false, editingEnabled && canRedo))
                    commands.push_back(WorkspaceCommand::Redo);
                ImGui::Separator();

                if (ImGui::MenuItem("Deselect", "Esc", false, editingEnabled))
                {
                    selectBody(-1);
                    commands.push_back(WorkspaceCommand::Deselect);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                for (int i = 0; i < 5; ++i)
                    ImGui::MenuItem(panelNames[i], nullptr, &panels[i]);
                ImGui::Separator();
                ImGui::MenuItem("Show Gizmos", nullptr, &showGizmos);

                if (ImGui::MenuItem("Hide Interface", "F1"))
                    visible = false;
                ImGui::MenuItem("Lock Layout", nullptr, &lockedLayout);
                tip("Keep panels docked while allowing splitter resizing.");

                if (ImGui::MenuItem("Reset Layout"))
                {
                    resetLayout = true;

                    for (bool &p : panels)
                        p = false;
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("Controls and Shortcuts", nullptr, &controlsOpen);
                ImGui::MenuItem("About", nullptr, &aboutOpen);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        auto display = ImGui::GetIO().DisplaySize;
        float statusHeight = visible ? ImGui::GetFrameHeight() + 8 * uiScale : 0;
        ImGui::SetNextWindowPos({0, menuHeight});
        ImGui::SetNextWindowSize({display.x, std::max(1.f, display.y - menuHeight - statusHeight)});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("Workspace", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoBackground);
        dockId = ImGui::GetID("WorkspaceDock");

        if (visible && (resetLayout || !ImGui::DockBuilderGetNode(dockId)))
        {
            ImGui::DockBuilderRemoveNode(dockId);
            ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetWindowSize());
            ImGuiID center = dockId;
            auto bottom = ImGui::DockBuilderSplitNode(
                center, ImGuiDir_Down, std::clamp(300 * uiScale / ImGui::GetWindowHeight(), 0.25f, 0.45f),
                nullptr, &center);
            auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.16f, nullptr, &center);
            auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.29f, nullptr, &center);
            ImGui::DockBuilderDockWindow("Scene", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Camera", right);
            ImGui::DockBuilderDockWindow("Render Settings", right);
            ImGui::DockBuilderDockWindow("Timeline", bottom);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderFinish(dockId);
            resetLayout = false;
        }

        ImGui::DockSpace(dockId, {0, 0},
                         visible ? (lockedLayout ? ImGuiDockNodeFlags_NoUndocking : 0)
                                 : ImGuiDockNodeFlags_KeepAliveOnly);
        ImGui::End();
        ImGui::PopStyleVar(2);

        if (visible && controlsOpen)
        {
            ImGui::SetNextWindowSize({460 * uiScale, 430 * uiScale}, ImGuiCond_Appearing);
            ImGui::Begin("Controls and Shortcuts", &controlsOpen,
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(
                "Compose\n  Click a body or Scene row to select.\n  Drag the XYZ arrows or "
                "center to move it.\n  Click empty space or press Esc to deselect.");
            ImGui::Separator();
            ImGui::TextUnformatted("Navigate (viewport focused)\n  Left/right drag: look around\n  WASD / "
                                   "arrows: move; Q / E: up / "
                                   "down\n  Shift: precision movement; F1: hide / restore interface");
            ImGui::Separator();
            ImGui::TextUnformatted(
                "Animate\n  Select a track, move the playhead, edit a pose, then Capture.\n  "
                "Drag diamonds to retime. Delete removes the selected key.\n  Ctrl+Z / "
                "Ctrl+Y: undo / redo pose and keyframe edits\n  Space (timeline focused): "
                "play / pause\n  Esc: stop playback, cancel export, or deselect");
            ImGui::Separator();
            ImGui::TextWrapped(
                "Observer velocity changes aberration and measured light, not camera movement. "
                "Hovering at zero is static; freely falling at zero follows a rain observer "
                "falling from rest at infinity. Combined observer speed stays below light speed.");
            ImGui::TextWrapped("Animation data is session-only. Closing the app loses keyframes. Workspace "
                               "layout is saved separately.");
            ImGui::PopTextWrapPos();
            ImGui::End();
        }

        if (visible && aboutOpen)
        {
            ImGui::SetNextWindowSize({340 * uiScale, 300 * uiScale}, ImGuiCond_Appearing);
            ImGui::Begin("About", &aboutOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted("Schwarzschild Ray Tracer");
            ImGui::TextWrapped(
                "An interactive black-hole scene and keyframe animation editor. See README for "
                "the physical model, limitations and texture credits.");
            ImGui::End();
        }
    }

    void SettingsPanel::drawRenderSettings(PanelActions &actions)
    {
        if (!panels[3])
            return;

        if (ImGui::Begin("Render Settings", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
        {
            ImGui::SeparatorText("Image");

            if (ImGui::BeginTable("Render values", 2, ImGuiTableFlags_SizingStretchProp))
            {
                auto field = [](const char* label)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-1);
                };
                field("Samples / pixel");
                actions.renderChanged |= ImGui::InputInt("##samples", &draft.samples);
                tip("More samples reduce noise and increase rendering time.");
                field("Exposure");
                actions.renderChanged |= ImGui::InputFloat("##exposure", &draft.exposure, 0.1f, 1, "%.2f");
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Resolution");
            actions.renderChanged |= ImGui::Checkbox("Match viewport resolution", &matchWindow);
            ImGui::BeginDisabled(matchWindow);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.42f);
            actions.renderChanged |= ImGui::InputInt("##width", &draft.width, 0, 0);
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            actions.renderChanged |= ImGui::InputInt("##height", &draft.height, 0, 0);
            tip("Width x height in pixels. A fixed aspect ratio is letterboxed in the viewport.");
            ImGui::EndDisabled();
            ImGui::Spacing();
            actions.renderChanged |= ImGui::Checkbox("Apply redshift", &draft.redshift);
            tip("Gravitational and Doppler shifts in measured color and brightness.");

            if (ImGui::CollapsingHeader("Advanced"))
            {
                ImGui::TextUnformatted("Ray integration");
                ImGui::SetNextItemWidth(-1);
                int mode = int(draft.integrationMode);
                const char* modes[] = {"Fixed radius", "Full scene", "Adaptive cutoff"};

                if (ImGui::Combo("##integration", &mode, modes, 3))
                {
                    draft.integrationMode = RenderSettings::IntegrationMode(mode);
                    actions.renderChanged = true;
                }

                tip("Fixed radius integrates near the hole; full scene integrates everywhere; adaptive "
                    "cutoff "
                    "skips negligible curvature.");
                ImGui::TextUnformatted("Random seed");
                ImGui::SetNextItemWidth(-1);
                actions.renderChanged |= ImGui::InputScalar("##seed", ImGuiDataType_U32, &draft.seed);
                tip("A fixed seed makes repeated renders reproducible on the same backend.");
            }
        }

        ImGui::End();
    }

    void SettingsPanel::drawCamera(PanelActions &actions, double &slowStep, const CameraData &camera,
                                   const SceneData &scene)
    {
        if (!panels[2])
            return;

        if (ImGui::Begin("Camera", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
        {
            ImGui::SeparatorText("Navigation");
            ImGui::TextUnformatted("Precision movement");
            const double low = 1e-8, high = std::nextafter(0.05, 0.0);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderScalar("##precision", ImGuiDataType_Double, &slowStep, &low, &high, "%.6g",
                                ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            tip("Movement step while holding Shift. Ctrl+click to enter an exact value.");
            ImGui::TextDisabled("Shift speed: %.5g units/s", 20 * slowStep);

            if (ImGui::Button("Reset camera pose"))
                actions.resetCamera = true;
            if (ImGui::Button("Edit camera position"))
            {
                selectCamera();
                panels[1] = true;
                ImGui::SetWindowFocus("Inspector");
            }

            if (ImGui::CollapsingHeader("Observer physics", ImGuiTreeNodeFlags_DefaultOpen))
            {
                int type = int(draft.observerType);
                const char* modes[] = {"Hovering", "Freely falling"};
                ImGui::SetNextItemWidth(-1);

                if (ImGui::Combo("##observer", &type, modes, 2))
                {
                    draft.observerType = RenderSettings::ObserverType(type);
                    actions.renderChanged = true;
                }

                tip("Observer velocity changes aberration and brightness, not position. See Help for "
                    "reference-frame definitions.");
                const char* axes[] = {"Left  /  Right", "Down  /  Up", "Backward  /  Forward"};

                for (int axis = 0; axis < 3; ++axis)
                {
                    ImGui::PushID(axis);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(axes[axis]);
                    ImGui::SameLine();
                    float zeroWidth = ImGui::CalcTextSize("Zero").x + 2 * ImGui::GetStyle().FramePadding.x;
                    ImGui::SetCursorPosX(
                        std::max(ImGui::GetCursorPosX(),
                                 ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - zeroWidth));
                    if (ImGui::SmallButton("Zero"))
                    {
                        draft.observerVelocity = editObserverComponent(draft.observerVelocity, axis, 0);
                        actions.renderChanged = true;
                    }

                    double value = axis == 0   ? draft.observerVelocity.x
                                   : axis == 1 ? draft.observerVelocity.y
                                               : draft.observerVelocity.z;
                    const double minimum = -0.999, maximum = 0.999;
                    ImGui::SetNextItemWidth(-1);

                    if (ImGui::SliderScalar("##velocity", ImGuiDataType_Double, &value, &minimum, &maximum,
                                            "%+.3f c", ImGuiSliderFlags_AlwaysClamp))
                    {
                        draft.observerVelocity = editObserverComponent(draft.observerVelocity, axis, value);
                        actions.renderChanged = true;
                    }

                    tip("Signed velocity relative to the camera. Other components are preserved; the edited "
                        "component is limited by the combined speed.");
                    auto left = ImGui::GetItemRectMin(), right = ImGui::GetItemRectMax();
                    auto* draw = ImGui::GetWindowDrawList();
                    float mid = (left.x + right.x) / 2;
                    draw->AddLine({mid, right.y - 4 * uiScale}, {mid, right.y},
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    auto labels = ImGui::GetCursorScreenPos();
                    auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    draw->AddText({left.x, labels.y}, color, "-0.999 c");
                    draw->AddText({mid - ImGui::CalcTextSize("0").x / 2, labels.y}, color, "0");
                    draw->AddText({right.x - ImGui::CalcTextSize("+0.999 c").x, labels.y}, color, "+0.999 c");
                    ImGui::Dummy({0, ImGui::GetTextLineHeight()});
                    ImGui::PopID();
                }

                ImGui::Text("Combined speed: %.4f c",
                            std::sqrt(dot(draft.observerVelocity, draft.observerVelocity)));
                if (ImGui::Button("Reset velocity"))
                {
                    draft.observerVelocity = {};
                    actions.renderChanged = true;
                }

                for (auto sphere : scene.spheres)
                    if (scene.materials[sphere.material.x].kindTexture.x == Schwarzschild)
                    {
                        Vec3 offset = camera.position - Vec3(sphere.centerRadius.x, sphere.centerRadius.y,
                                                             sphere.centerRadius.z);
                        double radius = std::sqrt(dot(offset, offset));
                        ImGui::TextDisabled("Distance to center: %.4g", radius);

                        if (radius <= 1 && draft.observerType == RenderSettings::Hovering)
                            ImGui::TextWrapped(
                                "Hovering is undefined at or inside the horizon. Choose Freely "
                                "falling to view the interior.");
                        if (radius <= 1e-4)
                            ImGui::TextWrapped("No physical observer frame is defined at the singularity.");
                    }
            }
        }

        ImGui::End();
    }

    void SettingsPanel::drawViewport(const PanelActions &actions, const CameraData &camera,
                                     const SceneData &scene, double aspect)
    {
        viewportHovered = viewportFocused = false;
        gizmoVisible = false;
        contentRect = {};
        imageRect = {};

        if (!visible)
        {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        int flags =
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowCollapsed(false);
        ImGuiWindowClass viewportClass;
        viewportClass.DockNodeFlagsOverrideSet =
            ImGuiDockNodeFlags_NoDockingOverMe | ImGuiDockNodeFlags_NoUndocking;
        ImGui::SetNextWindowClass(&viewportClass);

        if (lockedLayout)
            flags |= ImGuiWindowFlags_NoMove;
        if (!visible)
            flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(visible ? "Viewport" : "Presentation", nullptr, flags))
        {
            auto pos = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
            contentRect = {pos.x, pos.y, std::max(0.f, size.x), std::max(0.f, size.y)};
            double imageAspect =
                displayedImage.width > 0 ? double(displayedImage.width) / displayedImage.height : aspect;
            imageRect = contentRect.fit(imageAspect);

            if (imageRect.width > 0 && imageRect.height > 0)
            {
                ImGui::SetCursorScreenPos({imageRect.x, imageRect.y});

                if (displayedImage.texture)
                    ImGui::Image(ImTextureRef(ImTextureID(displayedImage.texture)),
                                 {imageRect.width, imageRect.height}, {0, 1}, {1, 0});
                else
                    ImGui::Dummy({imageRect.width, imageRect.height});
                viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                  imageRect.contains(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
                viewportFocused = ImGui::IsWindowFocused();
                drawGizmo(actions, camera, scene, imageAspect);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    PanelActions SettingsPanel::draw(double &slowStep, const GpuProgress &progress,
                                     const RenderSettings &active, bool preview, const CameraData &camera,
                                     const SceneData &scene)
    {
        PanelActions actions;
        actions.save = saveRequested;
        saveRequested = false;
        drawWorkspace(actions);

        if (visible)
        {
            ImGui::BeginDisabled(!editingEnabled);
            ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);

            if (panels[0])
            {
                if (ImGui::Begin("Scene", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
                {
                    ImGui::TextDisabled("OBJECTS");

                    if (ImGui::Selectable("Camera", cameraSelected))
                        selectCamera();
                    ImGui::Separator();

                    for (int i = 0; i < int(scene.spheres.size()); ++i)
                    {
                        if (scene.materials[scene.spheres[i].material.x].kindTexture.x == Environment)
                            continue;

                        ImGui::PushID(i);

                        if (ImGui::Selectable(nameOf(scene, i), selectedBody == i))
                            selectBody(i);
                        ImGui::PopID();
                    }

                    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(0))
                        selectBody(-1);
                }

                ImGui::End();
            }

            drawBodyEditor(actions, camera, scene, double(active.width) / active.height);
            drawCamera(actions, slowStep, camera, scene);
            drawRenderSettings(actions);
            ImGui::PopItemFlag();
            ImGui::EndDisabled();
        }

        drawViewport(actions, camera, scene, double(active.width) / active.height);

        if (visible)
        {
            auto display = ImGui::GetIO().DisplaySize;
            float h = ImGui::GetFrameHeight() + 8 * uiScale;
            ImGui::SetNextWindowPos({0, display.y - h});
            ImGui::SetNextWindowSize({display.x, h});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14 * uiScale, 4 * uiScale});
            ImGui::Begin("Status", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollbar);
            if (statusUntilRenderFinished && progress.finished && !preview)
            {
                status.clear();
                statusUntilRenderFinished = false;
            }

            char location[160]{};

            for (const auto &sphere : scene.spheres)
                if (scene.materials[sphere.material.x].kindTexture.x == Schwarzschild)
                {
                    Vec3 offset = camera.position -
                                  Vec3(sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z);
                    std::snprintf(location, sizeof(location),
                                  "BH center distance: %.5g  |  Horizon radius: 1",
                                  std::sqrt(dot(offset, offset)));
                    break;
                }
            auto start = ImGui::GetCursorScreenPos();
            float right = start.x + ImGui::GetContentRegionAvail().x;
            float locationX = std::max(start.x, right - ImGui::CalcTextSize(location).x);
            auto* draw = ImGui::GetWindowDrawList();
            // Reserve the right side even when a long filename is reported on the left.
            draw->PushClipRect(
                start, {std::max(start.x, locationX - 16 * uiScale), start.y + ImGui::GetTextLineHeight()},
                true);
            ImGui::TextDisabled("%s  |  %d x %d  |  %.1f / %d samples",
                                preview             ? "Preview"
                                : progress.finished ? "Ready"
                                                    : "Rendering",
                                active.width, active.height, progress.meanSamples, active.samples);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("GPU batch: %.2f ms | invalid rays: %llu", progress.lastBatchMilliseconds,
                                  static_cast<unsigned long long>(progress.failures));
            if (!status.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(statusError ? ImVec4(1, 0.55f, 0.45f, 1) : ImVec4(0.65f, 0.77f, 0.85f, 1),
                                   " | %s", status.c_str());
                tip(status.c_str());
            }

            draw->PopClipRect();
            ImGui::SetCursorScreenPos({locationX, start.y});
            ImGui::TextDisabled("%s", location);
            tip("Camera distance from the black-hole center and event horizon radius, in scene units.");
            ImGui::End();
            ImGui::PopStyleVar();
        }

        return actions;
    }

    void SettingsPanel::render()
    {
        ImGui::Render();
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.035f, 0.04f, 0.05f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void SettingsPanel::syncResolution(int width, int height)
    {
        if (matchWindow)
        {
            draft.width = width;
            draft.height = height;
        }
    }

    void SettingsPanel::setStatus(const std::string &text, bool error, bool untilRenderFinished)
    {
        status = text;
        statusError = error;
        statusUntilRenderFinished = untilRenderFinished;
    }
} // namespace rt
