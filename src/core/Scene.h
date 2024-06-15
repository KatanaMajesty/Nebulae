#pragma once

#include <vector>
#include "../nri/StaticMesh.h"
#include "InspectCamera.h"

#include "../input/Mouse.h"

namespace Neb
{

    struct Scene
    {
        void OnMouseScroll(const MouseEvent_Scrolled& event);
        void OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event);
        void OnMouseButtonInteract(const MouseEvent_ButtonInteraction& event);

        std::vector<nri::StaticMesh> StaticMeshes;
        InspectCamera Camera;
    
    private:
        // TODO: Camera related stuff. Will be moved/removed
        Vec2 m_prevCursorPos;
        bool m_ableToInspect = false;
    };

} // Neb namespace