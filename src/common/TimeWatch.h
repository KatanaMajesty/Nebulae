#pragma once

#include <chrono>

namespace Neb
{
    
    namespace dur
    {
        using Milliseconds = std::chrono::duration<int64_t, std::milli>;
        using Seconds = std::chrono::duration<int64_t>;
        using SecondsF32 = std::chrono::duration<float>;
    } // dur namespace

    template<typename Dur>
    class TimeWatch
    {
    public:
        using DurationType = Dur;
        using ClockType = std::chrono::system_clock;
        using Timestamp = std::chrono::time_point<ClockType>;

        static constexpr Timestamp InvalidTimestamp = Timestamp::min();

        // Resets the watch, updating the begining timestamp
        void Begin() noexcept 
        { 
            m_begin = ClockType::now(); 
        }
        
        DurationType ElapsedDuration() noexcept
        {
            Timestamp instant = ClockType::now();
            ClockType::duration elapsed = instant - m_begin;
            return std::chrono::duration_cast<DurationType>(elapsed);
        }

        DurationType::rep Elapsed() noexcept
        {
            return ElapsedDuration().count();
        }

    private:
        Timestamp m_begin = InvalidTimestamp;
    };

} // Neb namespace