#pragma once

#include <vector>
#include "../nri/StaticMesh.h"
#include "InspectCamera.h"

#include "../input/Mouse.h"

namespace Neb
{

    struct Scene
    {
        // Define rotation extent of inspection camera by X and Y axes
        static constexpr Vec2 RotationAngles = Vec2(180.0f, 90.0f);

        void OnMouseScroll(const MouseEvent_Scrolled& event);
        void OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event);
        void OnMouseButtonInteract(const MouseEvent_ButtonInteraction& event);

        std::vector<nri::StaticMesh> StaticMeshes;

        // TODO: Camera related stuff. Will be moved/removed
        InspectCamera Camera;
        bool AbleToInspect = false;
    };

} // Neb namespace