#pragma once

#include "Mouse.h"

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

    private:
        Mouse m_mouse;
    };

}