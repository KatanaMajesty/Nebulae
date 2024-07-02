#include "Keyboard.h"

#include "../common/Assert.h"

namespace Neb
{

    void Keyboard::SetKeycodeState(EKeycode keycode, EKeycodeState state)
    {
        NEB_ASSERT(keycode < eKeycode_NumKeycodes, "Wrong keycode, out of bounds!"); // out of bounds

        // Firstly update states, as we guarantee this update to happen before any callbacks
        EKeycodeState prevState = m_keycodeStates[keycode];
        m_keycodeStates[keycode] = state;

        if (prevState != state)
        {
            KeyboardEvent_KeyInteraction event = KeyboardEvent_KeyInteraction{
                .Keycode = keycode,
                .PrevState = prevState,
                .NextState = state,
            };

            for (const auto& callback : m_keyInteractionCallbacks)
                std::invoke(callback, event);
        }
    }

} // Neb namespace