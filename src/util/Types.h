#pragma once

#include <utility>
#include <type_traits>

namespace Neb
{

    template<typename Enum>
    constexpr std::underlying_type_t<Enum> EnumValue(Enum e) noexcept
    {
        return std::to_underlying<Enum>(e);
    }

} // Neb namespace