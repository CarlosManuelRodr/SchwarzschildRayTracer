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

        const char* bodyName(const SceneData &scene, int body)
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
        ++bodySelectionRevision;
        selectedBody = body;
        editorOpen = body >= 0;
        cameraSelected = false;
        dragAxis = -1;
        gizmoVisible = false;
        bodyEditPending = false;
    }

    void SettingsPanel::processGizmoEvent(const SDL_Event &event)
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || event.type == SDL_EVENT_WINDOW_MINIMIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE)
        {
            dragAxis = -1;
            gizmoVisible = false;
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && visible &&
            gizmoVisible && viewportHovered && imageRect.contains(event.button.x, event.button.y))
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

    void SettingsPanel::drawBodyEditor(PanelActions &actions, const CameraData &camera,
                                       const SceneData &scene, double)
    {
        if (originalBodyPositions.empty())
            for (auto sphere : scene.spheres)
                originalBodyPositions.push_back(
                    {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z});
        gizmoVisible = false;

        if (selectedBody < 0 || selectedBody >= int(scene.spheres.size()))
        {
            if (!panels[1])
                return;

            if (ImGui::Begin("Inspector", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
            {
                if (cameraSelected)
                {
                    ImGui::SeparatorText("Camera position");
                    double values[] = {camera.position.x, camera.position.y, camera.position.z};
                    const char* axes[] = {"X", "Y", "Z"};

                    for (int i = 0; i < 3; ++i)
                    {
                        ImGui::SetNextItemWidth(-36 * uiScale);

                        if (ImGui::InputDouble(axes[i], &values[i], 0, 0, "%.8g") &&
                            finitePosition({values[0], values[1], values[2]}))
                        {
                            actions.positionChanged = true;
                            actions.position = {values[0], values[1], values[2]};
                        }
                    }

                    if (ImGui::Button("Reset camera pose"))
                        actions.resetCamera = true;
                }
                else
                    ImGui::TextWrapped("Select an object in the scene or viewport to edit its position.");
            }

            ImGui::End();

            return;
        }

        auto sphere = scene.spheres[selectedBody];
        displayedBodyPosition = {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z};

        if (bodyEditPending)
        {
            actions.body = selectedBody;
            actions.bodyPosition = pendingBodyPosition;
            displayedBodyPosition = pendingBodyPosition;
            bodyEditPending = false;
        }

        if (!panels[1])
            return;

        if (ImGui::Begin("Inspector", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
        {
            ImGui::SeparatorText(bodyName(scene, selectedBody));
            double position[] = {displayedBodyPosition.x, displayedBodyPosition.y, displayedBodyPosition.z};
            const char* axes[] = {"X", "Y", "Z"};

            for (int i = 0; i < 3; ++i)
            {
                ImGui::SetNextItemWidth(-36 * uiScale);

                if (ImGui::InputDouble(axes[i], &position[i], 0, 0, "%.8g") &&
                    finitePosition({position[0], position[1], position[2]}))
                {
                    actions.body = selectedBody;
                    actions.bodyPosition = {position[0], position[1], position[2]};
                }
            }

            if (ImGui::Button("Reset position"))
            {
                actions.body = selectedBody;
                actions.bodyPosition = originalBodyPositions[selectedBody];
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore this body's original scene position.");
            if (scene.materials[scene.spheres[selectedBody].material.x].kindTexture.x == Schwarzschild)
                ImGui::TextDisabled("Disk and gravity follow this position.");
        }

        ImGui::End();
    }

    void SettingsPanel::drawGizmo(const PanelActions &actions, const CameraData &camera,
                                  const SceneData &scene, double aspect)
    {
        if (!visible || !showGizmos || !editingEnabled || selectedBody < 0 ||
            selectedBody >= int(scene.spheres.size()))
            return;

        ImVec2 display{imageRect.width, imageRect.height};
        auto selected = scene.spheres[selectedBody].centerRadius;
        displayedBodyPosition =
            actions.body == selectedBody ? actions.bodyPosition : Vec3(selected.x, selected.y, selected.z);
        bool anchored = selectedBody < int(bodyAnchors.size());

        if (anchored && bodyAnchors[selectedBody].z == 0)
            return;
        // World axes remain editing directions; their origin follows the rendered image.
        auto basis = camera.basis(aspect);
        gizmoRight = normalized(basis[2]);
        gizmoUp = normalized(basis[3]);
        Vec3 forward = normalized(camera.lookAt - camera.position);
        double tanHalf = std::tan(camera.verticalFov * 3.141592653589793 / 360);
        double depth = dot(displayedBodyPosition - camera.position, forward);

        if ((!anchored && depth <= 1e-4) || display.x <= 0 || display.y <= 0)
            return;

        Vec3 projectionOrigin = displayedBodyPosition;

        if (anchored && depth <= 1e-4)
        {
            depth = std::max(1.0, std::sqrt(dot(displayedBodyPosition - camera.position,
                                                displayedBodyPosition - camera.position)));
            projectionOrigin = camera.position + depth * forward;
        }

        auto project = [&](Vec3 point)
        {
            Vec3 offset = point - displayedBodyPosition + projectionOrigin - camera.position;
            double z = dot(offset, forward);

            if (z <= 1e-4)
                return SDL_FPoint{-100000, -100000};

            return SDL_FPoint{
                float(imageRect.x + display.x * (0.5 + dot(offset, gizmoRight) / (2 * z * tanHalf * aspect))),
                float(imageRect.y + display.y * (0.5 - dot(offset, gizmoUp) / (2 * z * tanHalf)))};
        };
        SDL_FPoint geometricOrigin = project(displayedBodyPosition);
        gizmoOrigin = anchored
                          ? SDL_FPoint{float(imageRect.x + bodyAnchors[selectedBody].x * display.x),
                                       float(imageRect.y + (1 - bodyAnchors[selectedBody].y) * display.y)}
                          : geometricOrigin;
        if (!imageRect.contains(gizmoOrigin.x, gizmoOrigin.y))
            return;

        gizmoPixelScale = 2 * depth * tanHalf / display.y;
        gizmoLength = std::min(depth * 0.3, gizmoPixelScale * 90 * uiScale);
        Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        ImU32 colors[] = {IM_COL32(245, 75, 75, 255), IM_COL32(90, 230, 110, 255),
                          IM_COL32(85, 150, 255, 255)};
        const char* labels[] = {"X", "Y", "Z"};
        auto draw = ImGui::GetWindowDrawList();
        draw->PushClipRect({imageRect.x, imageRect.y},
                           {imageRect.x + imageRect.width, imageRect.y + imageRect.height}, true);
        ImVec2 start{gizmoOrigin.x, gizmoOrigin.y};

        for (int i = 0; i < 3; ++i)
        {
            gizmoEnds[i] = project(displayedBodyPosition + gizmoLength * axes[i]);
            gizmoEnds[i].x += gizmoOrigin.x - geometricOrigin.x;
            gizmoEnds[i].y += gizmoOrigin.y - geometricOrigin.y;
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
        draw->PopClipRect();
        gizmoVisible = true;
    }
} // namespace rt
