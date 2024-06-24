#pragma once

namespace Neb
{

    template<typename EventType>
    struct InputEventCallbackContainer
    {
        using CallbackType = std::function<void(const EventType&)>;

        constexpr void Push(const CallbackType& callback) noexcept
        {
            Callbacks.push_back(callback);
        }

        // TODO: To support member function, this was the easiest user-friendly efficient way I came up with
        // Maybe there are better approaches?
        template<typename ConvertibleFunctor, typename... Args>
        constexpr void Push(ConvertibleFunctor&& callback, Args&&... args) noexcept
        {
            auto binding = [&callback, args...](const EventType& e) noexcept
                {
                    std::invoke(callback, args..., e);
                };
            Callbacks.push_back(binding);
        }

        constexpr void Clear() noexcept { Callbacks.clear(); }

        constexpr auto begin() noexcept { return Callbacks.begin(); }
        constexpr auto end() noexcept { return Callbacks.end(); }

        std::vector<CallbackType> Callbacks;
    };

} // Neb namespace