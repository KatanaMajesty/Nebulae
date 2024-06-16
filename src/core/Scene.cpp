#include "Scene.h"

#include "../common/Defines.h"
#include "../common/Log.h"

#include "../Nebulae.h"
#include "../input/InputManager.h"

namespace Neb
{

    void Scene::OnMouseScroll(const MouseEvent_Scrolled& event)
    {
        InputManager& inputManager = InputManager::Get();
        
        EMouseButtonStates rmbStates = inputManager.GetMouse().GetMouseButtonStates(eMouseButton_Right);
        float scrollFactor = (rmbStates & eMouseButtonState_Clicked) ? 0.3f : 0.1f;
        Camera.AddDistance(-event.Value * scrollFactor);
    }

    void Scene::OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event)
    {
        if (AbleToInspect)
        {
            // we want 180 degrees from center of the screen to its edge
            // to do that we need width/height
            Nebulae& nebulae = Nebulae::Get();
            NEB_ASSERT(nebulae.IsInitialized());

            Renderer& renderer = nebulae.GetRenderer();
            uint32_t width = renderer.GetWidth();
            uint32_t height = renderer.GetHeight();
            NEB_ASSERT(width > 0 && height > 0);

            int32_t dx = event.NextHotspot.X - event.PrevHotspot.X; // rotation across x plane (pitch)
            int32_t dy = event.NextHotspot.Y - event.PrevHotspot.Y; // rotation across y plane (yaw)
            
            // we need to swap them, as left/right would correspong to yaw rotation
            const float normDy = dy / static_cast<float>(width) * 2.0f;
            const float normDx = dx / static_cast<float>(height) * 2.0f;

            const Vec2 prevRotation = Camera.GetRotationXy();
            float rotX = normDy * RotationAngles.x;
            float rotY = normDx * RotationAngles.y;
            if (prevRotation.x + rotX < -89.9f || prevRotation.x + rotX > 89.9f)
                rotX = 0.0f;

            Camera.AddRotationXy(Vec2(rotX, rotY));
        }
    }

    void Scene::OnMouseButtonInteract(const MouseEvent_ButtonInteraction& event)
    {
        if (event.Button == eMouseButton_Left)
        {
            // ternary operator incident (fetish)
            AbleToInspect = (event.NextStates & eMouseButtonState_Released) ?
                false : 
                ((event.NextStates & eMouseButtonState_Clicked) ? true : false);
        }
    }

} // Neb namespace