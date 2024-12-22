#pragma once

#include "Log.h"

#include <string_view>
#include <source_location>
#include <print>
#include <format>

namespace Neb::detail
{

    template<typename... Args>
    inline void AssertPrint(std::string_view payload, const std::format_string<Args...> fmt, Args&&... args)
    {
        NEB_LOG_ERROR("Assertion at {} failed: {}", payload, std::format(fmt, std::forward<Args>(args)...));
    }

    inline void AssertPrint(std::string_view payload, std::string_view message)
    {
        NEB_LOG_ERROR("Assertion at {} failed: {}", payload, message);
    }

    inline void AssertPrint(std::string_view payload)
    {
        NEB_LOG_ERROR("Assertion at {} failed", payload);
    }

} // Neb::detail namespace

#if defined(NEB_DEBUG)
#define NEB_ASSERT(expr, ...)                                                               \
    do                                                                                      \
        if (!(expr))                                                                        \
        {                                                                                   \
            constexpr std::source_location _nebAssertLoc = std::source_location::current(); \
            Neb::detail::AssertPrint(std::format("{}:{}",                                   \
                                         _nebAssertLoc.function_name(),                     \
                                         _nebAssertLoc.line()),                             \
                ##__VA_ARGS__);                                                             \
            __debugbreak();                                                                 \
        }                                                                                   \
    while (false)
#else
#define NEB_ASSERT(expr, ...)
#endif