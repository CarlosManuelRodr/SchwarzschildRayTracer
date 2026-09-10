#include "SettingsPanel.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rt
{
SettingsPanel::SettingsPanel(SDL_Window* window, const RenderSettings& settings) : draft(settings)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    uiScale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window)));
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    ImGui::GetStyle().FontScaleDpi = uiScale;
    if (!ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext()))
    {
        ImGui::DestroyContext();
        throw std::runtime_error("Cannot initialize ImGui SDL3 backend");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 430 core"))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Cannot initialize ImGui OpenGL backend");
    }
}

SettingsPanel::~SettingsPanel()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void SettingsPanel::processEvent(const SDL_Event& event)
{
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F1)
    {
        visible = !visible;
        if (!visible)
            ImGui::SetWindowFocus(nullptr);
    }
}

bool SettingsPanel::capturesMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool SettingsPanel::capturesKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

void SettingsPanel::beginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

PanelActions SettingsPanel::draw(double& slowStep,
                                 const GpuProgress& progress,
                                 const RenderSettings& active,
                                 bool preview)
{
    PanelActions actions;
    if (!visible)
        return actions;

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(350.0f * uiScale, std::max(180.0f, display.x - 24)),
                                    std::min(560.0f * uiScale, std::max(100.0f, display.y - 24))),
                             ImGuiCond_FirstUseEver);
    // ImGui 1.92.9 defaults numeric fields to commit on Enter/focus loss.
    ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
    if (ImGui::Begin("Render settings", &visible))
    {
        ImGui::TextDisabled("F1: hide / show settings");
        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int mode = int(draft.integrationMode);
            const char* modes[] = {"Fixed radius", "Full scene", "Adaptive cutoff"};
            if (ImGui::Combo("Integration", &mode, modes, 3))
            {
                draft.integrationMode = RenderSettings::IntegrationMode(mode);
                actions.renderChanged = true;
            }
            actions.renderChanged |= ImGui::Checkbox("Gravitational / Doppler redshift", &draft.redshift);
            actions.renderChanged |= ImGui::InputInt("Samples", &draft.samples);
            actions.renderChanged |= ImGui::InputFloat("Exposure", &draft.exposure, 0.1f, 1.0f, "%.3f");
            ImGui::TextWrapped("Changes update automatically. The last complete image stays visible.");
        }

        if (ImGui::CollapsingHeader("Resolution and sampling"))
        {
            actions.renderChanged |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &draft.seed);
            actions.renderChanged |= ImGui::Checkbox("Match window resolution", &matchWindow);
            ImGui::BeginDisabled(matchWindow);
            actions.renderChanged |= ImGui::InputInt("Width", &draft.width);
            actions.renderChanged |= ImGui::InputInt("Height", &draft.height);
            ImGui::EndDisabled();
        }

        if (ImGui::CollapsingHeader("Navigation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const double minimum = 1e-8;
            const double maximum = std::nextafter(0.05, 0.0);
            double value = slowStep;
            if (ImGui::SliderScalar("Slow step",
                                    ImGuiDataType_Double,
                                    &value,
                                    &minimum,
                                    &maximum,
                                    "%.8g",
                                    ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            {
                if (std::isfinite(value))
                    slowStep = std::clamp(value, minimum, maximum);
            }
            value = slowStep;
            if (ImGui::InputDouble("Step value", &value, 0, 0, "%.10g"))
            {
                if (std::isfinite(value) && value > 0 && value < 0.05)
                {
                    slowStep = value;
                    setStatus("Movement step updated.");
                }
                else
                    setStatus("Slow step must be greater than 0 and less than 0.05.", true);
            }
            ImGui::TextWrapped("Drag the slider or Ctrl+click to type. Step value accepts exact values.");
            ImGui::Text("Shift movement: %.8g units/s", 20 * slowStep);
        }

        if (ImGui::Button("Save PNG"))
            actions.save = true;
        ImGui::Separator();
        ImGui::Text("%s: %d x %d", preview ? "Preview" : "Refining", active.width, active.height);
        ImGui::Text("Samples: %.1f / %d", progress.meanSamples, active.samples);
        ImGui::Text("GPU batch: %.2f ms", progress.lastBatchMilliseconds);
        if (!status.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  statusError ? ImVec4(1, 0.45f, 0.35f, 1) : ImVec4(0.6f, 0.9f, 0.7f, 1));
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::CollapsingHeader("Controls"))
            ImGui::TextWrapped("Left-drag: look. WASD / arrows: move. Q / E: up / down. Shift: precision. P: "
                               "save PNG. Escape: exit.");
    }
    ImGui::End();
    ImGui::PopItemFlag();
    return actions;
}

void SettingsPanel::render()
{
    ImGui::Render();
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

void SettingsPanel::setStatus(const std::string& message, bool error)
{
    status = message;
    statusError = error;
}
} // namespace rt
