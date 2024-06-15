#pragma once

#include <vector>
#include <functional>
#include <array>
#include <type_traits>
#include <stdexcept>

#include "../common/Defines.h"

namespace Neb
{

    // For future mouse implementation - https://learn.microsoft.com/en-us/windows/win32/inputdev/mouse-input
    
    enum EMouseButton : uint16_t
    {
        eMouseButton_Invalid = 0,
        eMouseButton_Left,
        eMouseButton_Right,
        eMouseButton_Middle,
        eMouseButton_NumButtons,
    };

    using EMouseButtonStates = uint8_t;
    enum  EMouseButtonState : uint8_t
    {
        eMouseButtonState_Released = 0,
        eMouseButtonState_ClickedOnce = 1,
        eMouseButtonState_ClickedTwice = 2,
        eMouseButtonState_Clicked = eMouseButtonState_ClickedOnce | eMouseButtonState_ClickedTwice,
    };

    // The mouse cursor contains a single-pixel point called the hot spot, 
    // a point that the system tracks and recognizes as the position of the cursor
    struct MouseCursorHotspot
    {
        constexpr bool operator==(const MouseCursorHotspot& other) const { return X == other.X && Y == other.Y; }
        constexpr bool operator!=(const MouseCursorHotspot& other) const { return !operator==(other); }

        int32_t X = 0;
        int32_t Y = 0;
    };

    // MouseEvent_Scrolled struct helps application to handle mouse wheel scroll events
    struct MouseEvent_Scrolled
    {
        // Stores the direction of a wheel scroll. 
        // If a scroll is up then positive (+1)
        // If a scroll is down then negative (-1)
        int32_t Value;

        // Hotspot defines the position where scroll event occured
        MouseCursorHotspot Hotspot;
    };

    // MouseEvent_CursorHotspotChanged struct helps to handle mouse movement, recorded by the application
    // Mouse movement when application's window is minimized is not recorded (afaik)
    struct MouseEvent_CursorHotspotChanged
    {
        // PrevHotspot is a hotspot that was previously stored by the application
        // After handling this event 
        MouseCursorHotspot PrevHotspot;

        // NextHotspot defines the new updated position of the cursor
        // it is guaranteed that at the moment of receiving the event the Mouse that have issued the event
        // already stores the new hotspot value in it
        MouseCursorHotspot NextHotspot;
    };

    // MouseEvent_ButtonInteraction struct helps application to handle button press events 
    struct MouseEvent_ButtonInteraction
    {
        // Button that the interaction is related to
        EMouseButton Button;

        // PrevStates flags define states that were previously defines for the
        // interacted button
        EMouseButtonStates PrevStates;

        // NextStates flags define new updated states of the button states
        // it is guaranteed that at the moment of receiving the event the interacted button for which interaction 
        // event was fired already stores the new hotspot value in it
        EMouseButtonStates NextStates;

        // Hotspot defines the position where button interaction event occured
        MouseCursorHotspot Hotspot;
    };

    template<typename EventType>
    struct MouseEventCallbackContainer
    {
        using CallbackType = std::function<void(const EventType&)>;

        constexpr void Push(const CallbackType& callback) noexcept
        {
            Callbacks.push_back(callback);
        }

        // TODO: To support member function, this was the easiest user-friendly efficient way I came up with
        // Maybe there are better approaches?
        template<typename ConvertibleFunctor, typename... Args>
        constexpr void Push(ConvertibleFunctor&& callback, Args&&... args) noexcept
        { 
            auto binding = [&callback, args...](const EventType& e) noexcept
                {
                    std::invoke(callback, args..., e);
                };
            Callbacks.push_back(binding);
        }

        constexpr void Clear() noexcept { Callbacks.clear(); }

        constexpr auto begin() noexcept { return Callbacks.begin(); }
        constexpr auto end() noexcept { return Callbacks.end(); }

        std::vector<CallbackType> Callbacks;
    };

    class Mouse
    {
    public:
        Mouse() = default;

        Mouse(const Mouse&) = delete;
        Mouse& operator=(const Mouse&) = delete;

        void NotifyWheelScroll(int32_t value);
        void SetMouseButtonStates(EMouseButton button, EMouseButtonStates states);
        EMouseButtonStates GetMouseButtonStates(EMouseButton button) const;

        void SetCursorHotspot(const MouseCursorHotspot& hotspot);
        const MouseCursorHotspot& GetCursorHotspot() const { return m_cursorHotspot; }

        template<typename EventType, typename CallbackType, typename... Args>
        void RegisterCallback(CallbackType&& callback, Args&&... args);

    private:
        template<typename EventType>
        MouseEventCallbackContainer<EventType>& GetEventCallbackContainer();

        std::array<EMouseButtonStates, eMouseButton_NumButtons> m_buttonStates = {};
        MouseCursorHotspot m_cursorHotspot = {};
    
        // Remark: Adding any more callback containers will require tweaking GetEventCallbackContainer() method
        MouseEventCallbackContainer<MouseEvent_Scrolled> m_mouseScrolledCallbacks;
        MouseEventCallbackContainer<MouseEvent_CursorHotspotChanged> m_hotspotChangedCallbacks;
        MouseEventCallbackContainer<MouseEvent_ButtonInteraction> m_buttonInteractionCallbacks;
    };

    template<typename EventType, typename CallbackType, typename... Args>
    inline void Mouse::RegisterCallback(CallbackType&& callback, Args&&... args)
    {
        MouseEventCallbackContainer<EventType>& callbackContainer = GetEventCallbackContainer<EventType>();
        callbackContainer.Push(std::forward<CallbackType>(callback), std::forward<Args>(args)...);
    }

    template<typename EventType>
    inline MouseEventCallbackContainer<EventType>& Mouse::GetEventCallbackContainer()
    {
        if constexpr (std::is_same_v<EventType, MouseEvent_Scrolled>)
            return m_mouseScrolledCallbacks;
        else if constexpr (std::is_same_v<EventType, MouseEvent_CursorHotspotChanged>)
            return m_hotspotChangedCallbacks;
        else if constexpr (std::is_same_v<EventType, MouseEvent_ButtonInteraction>)
            return m_buttonInteractionCallbacks;
        // Should not happen! do not use unsupported event types
        else throw std::runtime_error("Trying to access callback container of unsupported type");
    }

} // Neb namespace