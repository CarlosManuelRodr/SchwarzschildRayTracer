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

    void SettingsPanel::drawToolSelector()
    {
        ImGui::SeparatorText("Interaction tool");
        const char* names[] = {"Pointer", "Hand", "Rotate"};
        const char* tips[] = {"Select bodies and use translation handles. Drag empty space to look around.",
                              "Left-drag any body to move it in the view plane.",
                              "Select a body, then drag an X, Y, or Z rotation ring."};

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
                ImGui::SameLine();

            if (ImGui::RadioButton(names[i], int(activeTool) == i))
            {
                activeTool = InteractionTool(i);
                dragAxis = -1;
                bodyEditPending = false;
                gizmoVisible = false;
            }

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s Right-drag looks around in every tool.", tips[i]);
        }
    }

    void SettingsPanel::beginHandDrag(int body, SDL_FPoint point, const SceneData &scene,
                                      const CameraData &camera)
    {
        if (!editingEnabled || body < 0 || body >= int(scene.spheres.size()) || imageRect.height <= 0)
            return;

        if (selectedBody != body)
            selectBody(body);

        const auto &sphere = scene.spheres[body];
        dragBodyPosition = {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z};
        dragBodyRotation = bodyRotation(sphere);
        const auto basis = camera.basis(imageRect.width / imageRect.height);
        dragRight = normalized(basis[2]);
        dragUp = normalized(basis[3]);
        const double depth = std::max(0.01, std::abs(dot(dragBodyPosition - camera.position,
                                                         normalized(camera.lookAt - camera.position))));
        dragPixelScale =
            2 * depth * std::tan(camera.verticalFov * 3.141592653589793 / 360) / imageRect.height;
        dragStart = point;
        dragAxis = 3;
        rotatingBody = false;
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
            gizmoVisible && viewportHovered && imageRect.contains(event.button.x, event.button.y) &&
            activeTool != InteractionTool::Hand)
        {
            SDL_FPoint point{event.button.x, event.button.y};
            float nearest = 9 * uiScale;
            int axis = -1;
            int ringSegment = 0;

            for (int i = 0; i < 3; ++i)
            {
                if (activeTool == InteractionTool::Rotate)
                {
                    for (int j = 0; j < 64; ++j)
                    {
                        const auto a = rotationRings[i][j], b = rotationRings[i][j + 1];
                        const double dx = b.x - a.x, dy = b.y - a.y;
                        const double length2 = dx * dx + dy * dy;
                        const double t =
                            length2 > 1e-8
                                ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length2, 0.0,
                                             1.0)
                                : 0;
                        const float distance =
                            float(std::hypot(point.x - a.x - t * dx, point.y - a.y - t * dy));

                        if (distance < nearest)
                        {
                            nearest = distance;
                            axis = i;
                            ringSegment = j;
                        }
                    }
                }
                else
                {
                    float distance = distanceToSegment(point, gizmoOrigin, gizmoEnds[i]);

                    if (distance < nearest)
                    {
                        nearest = distance;
                        axis = i;
                    }
                }
            }

            if (activeTool == InteractionTool::Pointer &&
                std::hypot(point.x - gizmoOrigin.x, point.y - gizmoOrigin.y) < 8 * uiScale)
                axis = 3;

            if (axis >= 0)
            {
                dragAxis = axis;
                dragStart = point;
                dragBodyPosition = displayedBodyPosition;
                dragBodyRotation = displayedBodyRotation;
                dragLength = gizmoLength;
                dragPixelScale = gizmoPixelScale;
                dragRight = gizmoRight;
                dragUp = gizmoUp;
                rotatingBody = activeTool == InteractionTool::Rotate;

                if (rotatingBody)
                {
                    rotationCenter = gizmoOrigin;
                    rotationU = {rotationRings[axis][0].x - gizmoOrigin.x,
                                 rotationRings[axis][0].y - gizmoOrigin.y};
                    rotationV = {rotationRings[axis][16].x - gizmoOrigin.x,
                                 rotationRings[axis][16].y - gizmoOrigin.y};
                    const double angle = (ringSegment + 0.5) * 2 * 3.141592653589793 / 64;
                    rotationTangent = {float(-std::sin(angle) * rotationU.x + std::cos(angle) * rotationV.x),
                                       float(-std::sin(angle) * rotationU.y + std::cos(angle) * rotationV.y)};
                    const double det = rotationU.x * rotationV.y - rotationU.y * rotationV.x;
                    const double x = point.x - rotationCenter.x, y = point.y - rotationCenter.y;
                    rotationLastAngle = std::abs(det) > 1
                                            ? std::atan2((rotationU.x * y - rotationU.y * x) / det,
                                                         (x * rotationV.y - y * rotationV.x) / det)
                                            : 0;
                    rotationAngle = 0;
                }
                else if (axis < 3)
                    dragScreenAxis = {gizmoEnds[axis].x - gizmoOrigin.x, gizmoEnds[axis].y - gizmoOrigin.y};
            }
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION && dragAxis >= 0)
        {
            double dx = event.motion.x - dragStart.x, dy = event.motion.y - dragStart.y;
            pendingBodyPosition = dragBodyPosition;
            pendingBodyRotation = dragBodyRotation;

            if (rotatingBody)
            {
                const double det = rotationU.x * rotationV.y - rotationU.y * rotationV.x;

                if (std::abs(det) > 1)
                {
                    const double x = event.motion.x - rotationCenter.x, y = event.motion.y - rotationCenter.y;
                    const double angle = std::atan2((rotationU.x * y - rotationU.y * x) / det,
                                                    (x * rotationV.y - y * rotationV.x) / det);
                    rotationAngle += std::remainder(angle - rotationLastAngle, 2 * 3.141592653589793);
                    rotationLastAngle = angle;
                }
                else
                {
                    const double length2 =
                        rotationTangent.x * rotationTangent.x + rotationTangent.y * rotationTangent.y;
                    rotationAngle =
                        (dx * rotationTangent.x + dy * rotationTangent.y) / std::max(1.0, length2);
                }

                Vec3 angles;
                const double degrees = rotationAngle * 180 / 3.141592653589793;
                if (dragAxis == 0)
                    angles.x = degrees;
                if (dragAxis == 1)
                    angles.y = degrees;
                if (dragAxis == 2)
                    angles.z = degrees;
                pendingBodyRotation = composeRotation(rotationFromDegrees(angles), dragBodyRotation);
            }
            else
            {
                Vec3 delta;
                if (dragAxis == 3)
                    delta = dragPixelScale * (dx * dragRight - dy * dragUp);
                else
                {
                    double length2 =
                        dragScreenAxis.x * dragScreenAxis.x + dragScreenAxis.y * dragScreenAxis.y;
                    double amount = dragLength * (dx * dragScreenAxis.x + dy * dragScreenAxis.y) / length2;
                    if (dragAxis == 0)
                        delta.x = amount;
                    if (dragAxis == 1)
                        delta.y = amount;
                    if (dragAxis == 2)
                        delta.z = amount;
                }
                pendingBodyPosition = dragBodyPosition + delta;
            }

            bodyEditPending = finitePosition(pendingBodyPosition);
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            dragAxis = -1;
    }

    void SettingsPanel::drawBodyEditor(PanelActions &actions, const CameraData &camera,
                                       const SceneData &scene, double)
    {
        if (originalBodyPositions.empty())
        {
            for (const auto &sphere : scene.spheres)
            {
                originalBodyPositions.push_back(
                    {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z});
                originalBodyRotations.push_back(bodyRotation(sphere));
            }
        }

        gizmoVisible = false;
        const bool hasBody = selectedBody >= 0 && selectedBody < int(scene.spheres.size());

        if (hasBody)
        {
            const auto &sphere = scene.spheres[selectedBody];
            displayedBodyPosition = {sphere.centerRadius.x, sphere.centerRadius.y, sphere.centerRadius.z};
            displayedBodyRotation = bodyRotation(sphere);

            if (bodyEditPending)
            {
                displayedBodyPosition = pendingBodyPosition;
                displayedBodyRotation = pendingBodyRotation;
                actions.body = selectedBody;
                actions.bodyPosition = displayedBodyPosition;
                actions.bodyOrientation = displayedBodyRotation;
                bodyEditPending = false;
            }
        }

        if (!visible || !panels[1])
            return;

        if (ImGui::Begin("Inspector", nullptr, lockedLayout ? ImGuiWindowFlags_NoMove : 0))
        {
            drawToolSelector();

            if (hasBody || cameraSelected)
            {
                ImGui::SeparatorText(hasBody ? bodyName(scene, selectedBody) : "Camera position");
                Vec3 p = hasBody ? displayedBodyPosition : camera.position;
                double position[] = {p.x, p.y, p.z};
                const char* axes[] = {"X", "Y", "Z"};
                bool positionChanged = false;

                for (int i = 0; i < 3; ++i)
                {
                    ImGui::SetNextItemWidth(-36 * uiScale);
                    positionChanged |= ImGui::InputDouble(axes[i], &position[i], 0, 0, "%.8g");
                }

                if (positionChanged && finitePosition({position[0], position[1], position[2]}))
                {
                    if (hasBody)
                    {
                        displayedBodyPosition = {position[0], position[1], position[2]};
                        actions.body = selectedBody;
                    }
                    else
                    {
                        actions.positionChanged = true;
                        actions.position = {position[0], position[1], position[2]};
                    }
                }

                if (ImGui::Button(hasBody ? "Reset position" : "Reset camera pose"))
                {
                    if (hasBody)
                    {
                        displayedBodyPosition = originalBodyPositions[selectedBody];
                        actions.body = selectedBody;
                    }
                    else
                        actions.resetCamera = true;
                }

                if (hasBody)
                {
                    ImGui::SeparatorText("Rotation (degrees)");
                    const Vec3 euler = rotationDegrees(displayedBodyRotation);
                    double angles[] = {euler.x, euler.y, euler.z};
                    bool changed = false;
                    ImGui::PushID("rotation");
                    for (int i = 0; i < 3; ++i)
                    {
                        ImGui::SetNextItemWidth(-36 * uiScale);
                        changed |= ImGui::InputDouble(axes[i], &angles[i], 0, 0, "%.6f");
                    }
                    ImGui::PopID();

                    if (changed && finitePosition({angles[0], angles[1], angles[2]}))
                    {
                        displayedBodyRotation = rotationFromDegrees({angles[0], angles[1], angles[2]});
                        actions.body = selectedBody;
                    }

                    if (ImGui::Button("Reset rotation"))
                    {
                        displayedBodyRotation = originalBodyRotations[selectedBody];
                        actions.body = selectedBody;
                    }

                    ImGui::TextDisabled("XYZ angles; rotation gizmo uses world axes.");
                    if (scene.materials[scene.spheres[selectedBody].material.x].kindTexture.x ==
                        Schwarzschild)
                        ImGui::TextWrapped(
                            "Rotation turns the accretion disk. Schwarzschild gravity is spherical.");

                    if (actions.body == selectedBody)
                    {
                        actions.bodyPosition = displayedBodyPosition;
                        actions.bodyOrientation = displayedBodyRotation;
                    }
                }
            }
            else
                ImGui::TextWrapped("Select an object in the scene or viewport to edit its transform.");
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
        displayedBodyRotation = actions.body == selectedBody ? actions.bodyOrientation
                                                             : bodyRotation(scene.spheres[selectedBody]);
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

        if (activeTool == InteractionTool::Rotate)
        {
            const double radius = 75 * uiScale;
            for (int axis = 0; axis < 3; ++axis)
            {
                const Vec3 u = axes[(axis + 1) % 3], v = axes[(axis + 2) % 3];
                for (int j = 0; j <= 64; ++j)
                {
                    const double angle = j * 2 * 3.141592653589793 / 64;
                    const Vec3 direction = std::cos(angle) * u + std::sin(angle) * v;
                    auto &point = rotationRings[axis][j];
                    point = {float(start.x + radius * dot(direction, gizmoRight)),
                             float(start.y - radius * dot(direction, gizmoUp))};
                    if (j > 0)
                    {
                        const auto previous = rotationRings[axis][j - 1];
                        draw->AddLine({previous.x, previous.y}, {point.x, point.y},
                                      dragAxis == axis ? IM_COL32(255, 220, 90, 255) : colors[axis],
                                      3 * uiScale);
                    }
                }
                const auto label = rotationRings[axis][8];
                draw->AddText({label.x + 5, label.y + 5}, colors[axis], labels[axis]);
            }
            draw->AddText({start.x + 9, start.y - 20}, IM_COL32_WHITE, bodyName(scene, selectedBody));
            draw->PopClipRect();
            gizmoVisible = true;
            return;
        }

        if (activeTool == InteractionTool::Hand)
        {
            draw->AddCircle(start, 10 * uiScale, IM_COL32(255, 220, 90, 255), 24, 2 * uiScale);
            draw->PopClipRect();
            return;
        }

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
