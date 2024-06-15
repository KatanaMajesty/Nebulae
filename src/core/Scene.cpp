#include "Scene.h"

#include "../common/Defines.h"
#include "../common/Log.h"

namespace Neb
{

    void Scene::OnMouseScroll(const MouseEvent_Scrolled& event)
    {
        NEB_LOG_INFO("Delta is {}", event.Value);
        Camera.AddDistance(-event.Value * 0.1f);
    }

    void Scene::OnMouseCursorMoved(const MouseEvent_CursorHotspotChanged& event)
    {
        if (m_ableToInspect)
        {
            int32_t deltaX = event.NextHotspot.X - event.PrevHotspot.X;
            int32_t deltaY = event.PrevHotspot.Y - event.NextHotspot.Y;
            Camera.AddRotationXy(Vec2(deltaX, deltaY));

            NEB_LOG_INFO("{} {}", deltaX, deltaY);
        }
    }

    void Scene::OnMouseButtonInteract(const MouseEvent_ButtonInteraction& event)
    {
        if (event.Button == eMouseButton_Left)
        {
            // ternary operator incident (fetish)
            m_ableToInspect = (event.NextStates & eMouseButtonState_Released) ? 
                false : 
                ((event.NextStates & eMouseButtonState_Clicked) ? true : false);
        }
    }

} // Neb namespace