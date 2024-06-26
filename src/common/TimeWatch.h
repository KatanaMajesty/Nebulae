#pragma once

#include <chrono>

namespace Neb
{
    using SecondsF32 = std::chrono::duration<float>;

    class TimeWatch
    {
    public:
        using ClockType = std::chrono::system_clock;
        using DurationType = ClockType::duration;
        using Timestamp = std::chrono::time_point<ClockType, DurationType>;

        static constexpr Timestamp InvalidTimestamp = Timestamp::min();

        // Just a helper, exposed by API to allow for better timestamp management at external level
        inline Timestamp Now() const noexcept { return ClockType::now(); }

        // Resets the watch, updating the begining timestamp
        inline void Begin() noexcept { m_begin = ClockType::now(); }
        
        inline DurationType Elapsed() const noexcept
        {
            Timestamp instant = ClockType::now();
            return instant - m_begin;
        }

        template<typename To>
        To Elapsed() const noexcept { return std::chrono::duration_cast<To>(Elapsed()); }

    private:
        Timestamp m_begin = InvalidTimestamp;
    };

} // Neb namespace