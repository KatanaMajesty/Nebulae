#include "Mouse.h"

namespace Neb
{

    void Mouse::NotifyWheelScroll(int32_t value)
    {
        NEB_ASSERT(value == -1 || value == 1);
        
        MouseEvent_Scrolled event = MouseEvent_Scrolled{
            .Value = value,
            .Hotspot = m_cursorHotspot,
        };
    
        // TODO: (as of 18.02.2024) From WARP ENGINE -> Callbacks may be called from another thread. Racing is not handled here
        auto& callbackContainer = GetEventCallbackContainer<MouseEvent_Scrolled>();
        for (const auto& callback : callbackContainer)
            std::invoke(callback, event);
    }

    void Mouse::SetMouseButtonStates(EMouseButton button, EMouseButtonStates states)
    {
        NEB_ASSERT(button < eMouseButton_NumButtons); // out of bounds

        // Firstly update states, as we guarantee this update to happen before any callbacks
        EMouseButtonStates prevStates = m_buttonStates[button];
        m_buttonStates[button] = states;

        if (prevStates != states)
        {
            MouseEvent_ButtonInteraction event = MouseEvent_ButtonInteraction{
                .Button = button,
                .PrevStates = prevStates,
                .NextStates = states,
                .Hotspot = m_cursorHotspot
            };

            // TODO: (as of 18.02.2024) From WARP ENGINE -> Callbacks may be called from another thread. Racing is not handled here
            auto& callbackContainer = GetEventCallbackContainer<MouseEvent_ButtonInteraction>();
            for (const auto& callback : callbackContainer)
                std::invoke(callback, event);

        }
    }

    EMouseButtonStates Mouse::GetMouseButtonStates(EMouseButton button) const
    {
        NEB_ASSERT(button < eMouseButton_NumButtons); // out of bounds
        return m_buttonStates[button];
    }

    void Mouse::SetCursorHotspot(const MouseCursorHotspot& hotspot)
    {
        // Firstly update the hotspot, as we guarantee this update to happen before any callbacks
        MouseCursorHotspot prevHotspot = m_cursorHotspot;
        m_cursorHotspot = hotspot;

        if (prevHotspot != hotspot)
        {
            MouseEvent_CursorHotspotChanged event = MouseEvent_CursorHotspotChanged{
                .PrevHotspot = prevHotspot,
                .NextHotspot = hotspot
            };

            // TODO: (as of 18.02.2024) From WARP ENGINE -> Callbacks may be called from another thread. Racing is not handled here
            auto& callbackContainer = GetEventCallbackContainer<MouseEvent_CursorHotspotChanged>();
            for (const auto& callback : callbackContainer)
                std::invoke(callback, event);
        }
    }

} // Neb namespace