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
        Scoped& operator=(Scoped&& other)
        {
            if (this->ptr != &other)
            {
                this->ptr = other.ptr;
                other.ptr = nullptr;
            }
            return *this;
        }

        ~Scoped() { this->Release(); }

        bool IsValid() const { return this->ptr != nullptr; }

        T* operator->()
        {
            NEB_ASSERT(this->IsValid(), "Underlying pointer should be initialized");
            return ptr;
        }
        const T* operator->() const
        {
            NEB_ASSERT(this->IsValid(), "Underlying pointer should be initialized");
            return ptr;
        }

        T* operator&() { return this->ptr; }
        const T* operator&() const { return this->ptr; }

        operator T*() { return this->ptr; }
        operator const T*() const { return this->ptr; }

        // Frees the memory of underlying pointer
        void Release()
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
    Scoped<T> MakeScoped(Args&&... args)
    {
        return Scoped(new T(std::forward<Args>(args)...));
    }

}; // Neb namespace