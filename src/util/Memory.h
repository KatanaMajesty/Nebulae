#pragma once

#include "common/Assert.h"

#include <concepts>

#define NEB_CHECK_POW2_ALIGNMENT(a) NEB_ASSERT(::Neb::IsPow2(a), "Alignment should be power of 2 (was {})", a)

namespace Neb
{

    // checks if given number is power of 2
    template<std::integral T>
    inline constexpr T IsPow2(T v) noexcept
    {
        return static_cast<bool>(v) && !(v & (v - 1));
    }

    // Effectively rounds up the given size to match alignment
    // provided alignment should be power ot two
    template<std::integral T, std::convertible_to<T> U>
    inline constexpr T AlignUp(T v, U alignment) noexcept
    {
        NEB_CHECK_POW2_ALIGNMENT(alignment);
        return (v + (alignment - 1)) & ~(alignment - 1);
    }

    // checks if integral value is properly aligned to pow2 alignment
    template<std::integral T, std::convertible_to<T> U>
    inline constexpr bool IsAligned(T v, U alignment) noexcept
    {
        NEB_CHECK_POW2_ALIGNMENT(alignment);
        return !(v % alignment);
    }

}