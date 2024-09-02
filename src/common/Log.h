#pragma once

#include "Defines.h"
#include "Win.h"

#include <iostream>
#include <print>
#include <cassert>
#include <mutex>

namespace Neb
{

    // https://stackoverflow.com/questions/17125440/c-win32-console-color
    enum class EConsoleColor
    {
        Black = 0,
        DarkBlue = FOREGROUND_BLUE,
        DarkGreen = FOREGROUND_GREEN,
        DarkCyan = FOREGROUND_GREEN | FOREGROUND_BLUE,
        DarkRed = FOREGROUND_RED,
        DarkMagenta = FOREGROUND_RED | FOREGROUND_BLUE,
        DarkYellow = FOREGROUND_RED | FOREGROUND_GREEN,
        DarkGray = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
        Gray = FOREGROUND_INTENSITY,
        Blue = FOREGROUND_INTENSITY | FOREGROUND_BLUE,
        Green = FOREGROUND_INTENSITY | FOREGROUND_GREEN,
        Cyan = FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE,
        Red = FOREGROUND_INTENSITY | FOREGROUND_RED,
        Magenta = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_BLUE,
        Yellow = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN,
        White = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    };

    struct BareConsole
    {
        static BareConsole& Get() noexcept
        {
            static BareConsole instance;
            return instance;
        }
        
        mutable std::mutex OutputMutex;
        HANDLE OutputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        EConsoleColor CurrentColor = EConsoleColor::White;
    };

    template<typename... Args>
    void Trace(EConsoleColor color, const std::format_string<Args...> fmt, Args&&... args)
    {
        BareConsole& console = BareConsole::Get();
        {
            std::scoped_lock _(console.OutputMutex);
            assert(SetConsoleTextAttribute(console.OutputHandle, static_cast<WORD>(color)) && "Failed to set console color");
            {
                std::println(fmt, std::forward<Args>(args)...);
            }
            assert(SetConsoleTextAttribute(console.OutputHandle, static_cast<WORD>(console.CurrentColor)) && "Failed to set console color");
        }
    }

} // Neb namespace

#if defined(NEB_DEBUG)
#define NEB_LOG_INFO(msg, ...) (Neb::Trace(Neb::EConsoleColor::Gray, msg, ##__VA_ARGS__))
#define NEB_LOG_WARN(msg, ...) (Neb::Trace(Neb::EConsoleColor::Yellow, msg, ##__VA_ARGS__))
#define NEB_LOG_ERROR(msg, ...) (Neb::Trace(Neb::EConsoleColor::Red, msg, ##__VA_ARGS__))
#else
#define NEB_LOG_INFO(msg, ...)
#define NEB_LOG_WARN(msg, ...)
#define NEB_LOG_ERROR(msg, ...)
#endif