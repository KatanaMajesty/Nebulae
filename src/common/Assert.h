#pragma once

#include "Defines.h"

#include <source_location>
#include <print>

#if defined(NEB_DEBUG)
#define NEB_ASSERT(expr, ...)                                                                        \
    do                                                                                               \
        if (!(expr))                                                                                 \
        {                                                                                            \
            constexpr std::source_location _neb_loc = std::source_location::current();               \
            std::println("Assertion at {}:{} failed: {}", _neb_loc.function_name(), _neb_loc.line(), \
                std::format(__VA_ARGS__));                                                           \
            __debugbreak();                                                                          \
        }                                                                                            \
    while (false)
#else
#define NEB_ASSERT(expr, ...)
#endif