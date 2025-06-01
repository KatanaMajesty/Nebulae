#pragma once

#include <vector>
#include "../nri/StaticMesh.h"
#include "InspectCamera.h"

#include "input/Mouse.h"
#include "input/Keyboard.h"

namespace Neb
{

    struct Scene
    {
        // Define rotation extent of inspection camera by X and Y axes
        static constexpr Vec2 RotationAngles = Vec2(180.0f, 90.0f);

        void OnMouseScroll(const MouseEvent_Scrolled& event);
        void OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event);
        void OnMouseButtonInteract(const MouseEvent_ButtonInteraction& event);

        void OnKeyboardInteract(const KeyboardEvent_KeyInteraction& event);

        std::vector<nri::StaticMesh> StaticMeshes;

        // TODO: Camera related stuff. Will be moved/removed
        InspectCamera Camera;
        bool AbleToInspect = false;
        float ScrollSpeedFactor = 0.25f; // 0.5f if fast, 0.25f for default
    };

} // Neb namespace