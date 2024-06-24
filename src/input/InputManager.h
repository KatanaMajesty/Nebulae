#pragma once

#include "Mouse.h"
#include "Keyboard.h"

namespace Neb
{

    class InputManager
    {
    private:
        InputManager() = default;

    public:
        InputManager(const InputManager&) = delete;
        InputManager& operator=(const InputManager&) = delete;

        static InputManager& Get()
        {
            static InputManager instance;
            return instance;
        }

        Mouse& GetMouse() { return m_mouse; }
        const Mouse& GetMouse() const { return m_mouse; }
    
        Keyboard& GetKeyboard() { return m_keyboard; }
        const Keyboard& GetKeyboard() const { return m_keyboard; }

    private:
        Mouse m_mouse;
        Keyboard m_keyboard;
    };

}