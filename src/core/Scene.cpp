#include "Scene.h"

#include "../common/Assert.h"
#include "../common/Log.h"

#include "../Nebulae.h"
#include "../input/InputManager.h"

#include "../nri/imgui/UiContext.h"

namespace Neb
{

    void Scene::OnMouseScroll(const MouseEvent_Scrolled& event)
    {
        Camera.AddDistance(-event.Value * this->ScrollSpeedFactor);
    }

    void Scene::OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event)
    {
        if (nri::UiContext::Get()->IsMouseBusy())
            return;

        if (AbleToInspect)
        {
            // we want 180 degrees from center of the screen to its edge
            // to do that we need width/height
            Nebulae& nebulae = Nebulae::Get();
            NEB_ASSERT(nebulae.IsInitialized(), "Nebulae is not initialized here");

            const Renderer* renderer = nebulae.GetRenderer();
            uint32_t width = renderer->GetWidth();
            uint32_t height = renderer->GetHeight();
            NEB_ASSERT(width > 0 && height > 0, "Invalid cursor hotspot extents");

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
        if (nri::UiContext::Get()->IsMouseBusy())
            return;

        if (event.Button == eMouseButton_Left)
        {
            // ternary operator incident (fetish)
            AbleToInspect = (event.NextStates & eMouseButtonState_Released) ? false : ((event.NextStates & eMouseButtonState_Clicked) ? true : false);
        }
    }

    void Scene::OnKeyboardInteract(const KeyboardEvent_KeyInteraction& event)
    {
        if (nri::UiContext::Get()->IsKeyboardBusy())
            return;

        switch (event.Keycode)
        {
        case eKeycode_Shift:
            this->ScrollSpeedFactor = (event.NextState == eKeycodeState_Released) ? 0.25f : 0.5f; // if released reset to default
            break;
        case eKeycode_W:
        case eKeycode_A:
        case eKeycode_S:
        case eKeycode_D:
        default:
            return; // do nothing
        }
    }

} // Neb namespace