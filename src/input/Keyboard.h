#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <array>

#include "InputCallback.h"

namespace Neb
{

    // EKeycode only represents a key on a general-case keyboard. Some keyboards might not have physical representation of these keys.
    enum EKeycode : uint32_t
    {
        eKeycode_Invalid = 0,
        // F1 - F12 (we do not support F13 - F24)
        eKeycode_F1, eKeycode_F2, eKeycode_F3, eKeycode_F4, eKeycode_F5, eKeycode_F6,
        eKeycode_F7, eKeycode_F8, eKeycode_F9, eKeycode_F10, eKeycode_F11, eKeycode_F12,
        // Keybord nums 1 - 9 and 0
        eKeycode_1, eKeycode_2, eKeycode_3, eKeycode_4, eKeycode_5, eKeycode_6, eKeycode_7, eKeycode_8, eKeycode_9, eKeycode_0,
        eKeycode_A, eKeycode_B, eKeycode_C, eKeycode_D, eKeycode_E, eKeycode_F, eKeycode_G, eKeycode_H,
        eKeycode_I, eKeycode_J, eKeycode_K, eKeycode_L, eKeycode_M, eKeycode_N, eKeycode_O, eKeycode_P,
        eKeycode_Q, eKeycode_R, eKeycode_S, eKeycode_T, eKeycode_U, eKeycode_V, eKeycode_W, eKeycode_X,
        eKeycode_Y, eKeycode_Z,
        eKeycode_Esc,
        eKeycode_Shift, eKeycode_Ctrl,
        // Reserved to always be last
        eKeycode_NumKeycodes,
    };

    enum EKeycodeState : uint8_t
    {
        eKeycodeState_Released = 0,
        eKeycodeState_Pressed,
    };

    struct KeyboardEvent_KeyInteraction
    {
        // Simillarly to the MouseEvent_ButtonInteraction, keycode that the interaction is related to
        EKeycode Keycode;

        // PrevStates flags define states that were previously defines for the
        // interacted button
        EKeycodeState PrevState;

        // NextStates flags define new updated states of the button states
        // it is guaranteed that at the moment of receiving the event the interacted button for which interaction 
        // event was fired already stores the new hotspot value in it
        EKeycodeState NextState;
    };

    // For all of the WinAPI inputs dogma we chill here - https://learn.microsoft.com/en-us/windows/win32/learnwin32/keyboard-input
    // The basic idea of the system is to fulfill three distinct types of input, that are:
    // - Character input. Text that the user types into a document or edit box.
    // - Keyboard shortcuts (Ctrl + O to open a file, etc.)
    // - System commands (Alt + Tab, Win + Tab, Win + D, etc.) // Not sure if thats the case?
    class Keyboard
    {
    public:
        Keyboard() = default;

        Keyboard(const Keyboard&) = delete;
        Keyboard& operator=(const Keyboard&) = delete;

        Keyboard(Keyboard&&) = delete;
        Keyboard& operator=(Keyboard&&) = delete;

        void SetKeycodeState(EKeycode keycode, EKeycodeState state);
        EKeycodeState GetKeycodeState(EKeycode keycode) const { return m_keycodeStates[keycode]; }

        template<typename EventType, typename CallbackType, typename... Args>
        void RegisterCallback(CallbackType&& callback, Args&&... args);

    private:
        InputEventCallbackContainer<KeyboardEvent_KeyInteraction> m_keyInteractionCallbacks;
        std::array<EKeycodeState, eKeycode_NumKeycodes> m_keycodeStates = {};
    };

    template<typename EventType, typename CallbackType, typename ...Args>
    inline void Keyboard::RegisterCallback(CallbackType&& callback, Args && ...args)
    {
        // We only deal with key interaction as for now
        static_assert(std::is_same_v<EventType, KeyboardEvent_KeyInteraction>);
        m_keyInteractionCallbacks.Push(std::forward<CallbackType>(callback), std::forward<Args>(args)...);
    }

} // Neb namespace