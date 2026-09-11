#include "SettingsPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace rt
{
namespace
{
float distanceToSegment(SDL_FPoint p, SDL_FPoint a, SDL_FPoint b)
{
    float dx = b.x - a.x, dy = b.y - a.y;
    float length2 = dx * dx + dy * dy;
    if (length2 < 100)
        return std::numeric_limits<float>::max();
    float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.15f, 1.0f);
    return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

const char* bodyName(const SceneData& scene, int body)
{
    switch (scene.materials[scene.spheres[body].material.x].kindTexture.x)
    {
    case Earth:
        return "Earth";
    case Moon:
        return "Moon";
    case DiffuseLight:
        return "Sun";
    case Schwarzschild:
        return "Black hole";
    default:
        return "Body";
    }
}

bool finitePosition(Vec3 p)
{
    return std::isfinite(float(p.x)) && std::isfinite(float(p.y)) && std::isfinite(float(p.z));
}
} // namespace

void SettingsPanel::selectBody(int body)
{
    selectedBody = body;
    editorOpen = body >= 0;
    if (editorOpen)
        visible = true;
    dragAxis = -1;
    gizmoVisible = false;
    bodyEditPending = false;
}

void SettingsPanel::processGizmoEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
    {
        dragAxis = -1;
        gizmoVisible = false;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && visible &&
        gizmoVisible && !ImGui::GetIO().WantCaptureMouse)
    {
        SDL_FPoint point{event.button.x, event.button.y};
        float nearest = 9 * uiScale;
        int axis = -1;
        for (int i = 0; i < 3; ++i)
        {
            float distance = distanceToSegment(point, gizmoOrigin, gizmoEnds[i]);
            if (distance < nearest)
            {
                nearest = distance;
                axis = i;
            }
        }
        if (std::hypot(point.x - gizmoOrigin.x, point.y - gizmoOrigin.y) < 8 * uiScale)
            axis = 3;
        if (axis >= 0)
        {
            dragAxis = axis;
            dragStart = point;
            dragBodyPosition = displayedBodyPosition;
            dragLength = gizmoLength;
            dragPixelScale = gizmoPixelScale;
            dragRight = gizmoRight;
            dragUp = gizmoUp;
            if (axis < 3)
                dragScreenAxis = {gizmoEnds[axis].x - gizmoOrigin.x, gizmoEnds[axis].y - gizmoOrigin.y};
        }
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && dragAxis >= 0)
    {
        double dx = event.motion.x - dragStart.x, dy = event.motion.y - dragStart.y;
        Vec3 delta;
        if (dragAxis == 3)
            delta = dragPixelScale * (dx * dragRight - dy * dragUp);
        else
        {
            double length2 = dragScreenAxis.x * dragScreenAxis.x + dragScreenAxis.y * dragScreenAxis.y;
            double amount = dragLength * (dx * dragScreenAxis.x + dy * dragScreenAxis.y) / length2;
            if (dragAxis == 0)
                delta.x = amount;
            if (dragAxis == 1)
                delta.y = amount;
            if (dragAxis == 2)
                delta.z = amount;
        }
        pendingBodyPosition = dragBodyPosition + delta;
        bodyEditPending = finitePosition(pendingBodyPosition);
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
        dragAxis = -1;
}

void SettingsPanel::drawBodyEditor(PanelActions& actions,
                                   const CameraData& camera,
                                   const SceneData& scene,
                                   double aspect)
{
    if (originalBodyPositions.empty())
        for (auto sphere : scene.spheres)
            originalBodyPositions.push_back(
                {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z});
    gizmoVisible = false;
    if (!editorOpen || selectedBody < 0 || selectedBody >= int(scene.spheres.size()))
        return;
    auto sphere = scene.spheres[selectedBody];
    displayedBodyPosition = {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z};
    if (bodyEditPending)
    {
        actions.body = selectedBody;
        actions.bodyPosition = pendingBodyPosition;
        displayedBodyPosition = pendingBodyPosition;
        bodyEditPending = false;
    }
    auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(12, std::max(12.0f, display.y - 250 * uiScale)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350 * uiScale, 225 * uiScale), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Body transform", &editorOpen))
    {
        if (ImGui::BeginCombo("Selected", bodyName(scene, selectedBody)))
        {
            for (int i = 0; i < int(scene.spheres.size()); ++i)
            {
                if (scene.materials[scene.spheres[i].material.x].kindTexture.x == Environment)
                    continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(bodyName(scene, i), selectedBody == i))
                    selectBody(i);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        auto current = scene.spheres[selectedBody].centerRadius;
        double position[] = {current.x, current.y, current.z};
        if (ImGui::InputScalarN("Position XYZ", ImGuiDataType_Double, position, 3, nullptr, nullptr, "%.8g"))
        {
            Vec3 value{position[0], position[1], position[2]};
            if (finitePosition(value))
            {
                actions.body = selectedBody;
                actions.bodyPosition = value;
            }
            else
                ImGui::TextWrapped("Coordinates must be finite.");
        }
        if (ImGui::Button("Reset position"))
        {
            actions.body = selectedBody;
            actions.bodyPosition = originalBodyPositions[selectedBody];
        }
        ImGui::TextWrapped(
            "Drag X / Y / Z arrows to move on a world axis. Drag the center to move in the view plane.");
        if (scene.materials[scene.spheres[selectedBody].material.x].kindTexture.x == Schwarzschild)
            ImGui::TextWrapped("The disk and gravity field follow the black hole.");
    }
    ImGui::End();
    if (!editorOpen)
    {
        dragAxis = -1;
        return;
    }
    auto selected = scene.spheres[selectedBody].centerRadius;
    displayedBodyPosition =
        actions.body == selectedBody ? actions.bodyPosition : Vec3(selected.x, selected.y, selected.z);
    // Editor handles use geometric projection, independently of the lensed image.
    auto basis = camera.basis(aspect);
    gizmoRight = normalized(basis[2]);
    gizmoUp = normalized(basis[3]);
    Vec3 forward = normalized(camera.lookAt - camera.position);
    double tanHalf = std::tan(camera.verticalFov * 3.141592653589793 / 360);
    double depth = dot(displayedBodyPosition - camera.position, forward);
    if (depth <= 1e-4 || display.x <= 0 || display.y <= 0)
        return;
    auto project = [&](Vec3 point)
    {
        Vec3 offset = point - camera.position;
        double z = dot(offset, forward);
        if (z <= 1e-4)
            return SDL_FPoint{-100000, -100000};
        return SDL_FPoint{float(display.x * (0.5 + dot(offset, gizmoRight) / (2 * z * tanHalf * aspect))),
                          float(display.y * (0.5 - dot(offset, gizmoUp) / (2 * z * tanHalf)))};
    };
    gizmoOrigin = project(displayedBodyPosition);
    if (gizmoOrigin.x < 0 || gizmoOrigin.x > display.x || gizmoOrigin.y < 0 || gizmoOrigin.y > display.y)
        return;
    gizmoPixelScale = 2 * depth * tanHalf / display.y;
    gizmoLength = std::min(depth * 0.3, gizmoPixelScale * 90 * uiScale);
    Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    ImU32 colors[] = {IM_COL32(245, 75, 75, 255), IM_COL32(90, 230, 110, 255), IM_COL32(85, 150, 255, 255)};
    const char* labels[] = {"X", "Y", "Z"};
    auto draw = ImGui::GetBackgroundDrawList();
    ImVec2 start{gizmoOrigin.x, gizmoOrigin.y};
    for (int i = 0; i < 3; ++i)
    {
        gizmoEnds[i] = project(displayedBodyPosition + gizmoLength * axes[i]);
        ImVec2 end{gizmoEnds[i].x, gizmoEnds[i].y};
        float dx = end.x - start.x, dy = end.y - start.y, length = std::hypot(dx, dy);
        if (length < 10)
            continue;
        draw->AddLine(start, end, colors[i], 3 * uiScale);
        float ux = dx / length, uy = dy / length;
        draw->AddTriangleFilled(
            end,
            {end.x - 12 * uiScale * ux + 5 * uiScale * uy, end.y - 12 * uiScale * uy - 5 * uiScale * ux},
            {end.x - 12 * uiScale * ux - 5 * uiScale * uy, end.y - 12 * uiScale * uy + 5 * uiScale * ux},
            colors[i]);
        draw->AddText({end.x + 6, end.y + 6}, colors[i], labels[i]);
    }
    draw->AddCircleFilled(start, 6 * uiScale, IM_COL32(255, 220, 90, 255));
    draw->AddText({start.x + 9, start.y - 20}, IM_COL32_WHITE, bodyName(scene, selectedBody));
    gizmoVisible = true;
}
} // namespace rt
