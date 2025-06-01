#pragma once

#include "common/Assert.h"

#include <memory>
#include <functional>

namespace Neb
{

    template<typename T, typename Deleter = std::default_delete<T>>
    struct Scoped
    {
        Scoped() = default;
        Scoped(T* ptr)
            : ptr(ptr)
        {
        }

        Scoped(const Scoped&) = delete;
        Scoped& operator=(const Scoped&) = delete;

        Scoped(Scoped&& other)
            : ptr(other.ptr)
        {
            other.ptr = nullptr;
        }
        Scoped& operator=(Scoped&& other) noexcept
        {
            if (this->ptr != &other)
            {
                this->ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }

        ~Scoped() { this->Release(); }

        inline constexpr bool IsValid() const noexcept { return this->ptr != nullptr; }

        inline constexpr T* operator->() noexcept
        {
            NEB_ASSERT(this->IsValid(), "Underlying pointer should be initialized");
            return ptr;
        }
        inline constexpr const T* operator->() const noexcept
        {
            NEB_ASSERT(this->IsValid(), "Underlying pointer should be initialized");
            return ptr;
        }

        inline constexpr T* operator&() noexcept { return this->ptr; }
        inline constexpr const T* operator&() const noexcept { return this->ptr; }

        inline constexpr operator T*() noexcept { return this->ptr; }
        inline constexpr operator const T*() const noexcept { return this->ptr; }

        // Frees the memory of underlying pointer
        inline constexpr void Release()
        {
            if (this->IsValid())
            {
                std::invoke(Deleter(), ptr);
            }
            this->ptr = nullptr;
        }

        T* ptr = nullptr;
    };

    template<typename T, typename... Args>
    inline constexpr Scoped<T> MakeScoped(Args&&... args)
    {
        return Scoped(new T(std::forward<Args>(args)...));
    }

}; // Neb namespace