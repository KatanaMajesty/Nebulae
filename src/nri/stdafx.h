#pragma once

#include "../Win.h"
#include "common/Assert.h"

#include <stdexcept>
#include <string_view>
#include <format>
#include <filesystem>

#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dx12.h>

#define NEB_SET_HANDLE_NAME(t, str, ...) t->SetName(std::filesystem::path(std::format(str, __VA_ARGS__)).wstring().c_str())

#define CONSTANT_BUFFER_STRUCT struct alignas(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)

namespace Neb::nri
{

    static inline IID NullRIID = __uuidof(nullptr);

    template<typename T>
    using D3D12Rc = Microsoft::WRL::ComPtr<T>;

    template<typename T>
    using Rc = D3D12Rc<T>; // just a shorter alias

    class HrException : public std::runtime_error
    {
    public:
        HrException(HRESULT hr)
            : std::runtime_error(std::format("HRESULT of {:x}", static_cast<UINT>(hr)))
        {
        }
        HrException(HRESULT hr, std::string_view message)
            : std::runtime_error(std::format("HRESULT of {:x} -> {}", static_cast<UINT>(hr), message))
        {
        }
    };

    template<typename... Args>
    inline void ThrowIfFailed(HRESULT hr, const std::format_string<Args...> fmt, Args&&... args)
    {
        if (FAILED(hr))
        {
            NEB_ASSERT(false, fmt, std::forward<Args>(args)...);
            throw HrException(hr, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    inline void ThrowIfFailed(HRESULT hr, std::string_view message)
    {
        if (FAILED(hr))
        {
            NEB_ASSERT(false, message);
            throw HrException(hr, message);
        }
    }

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            NEB_ASSERT(false);
            throw HrException(hr);
        }
    }

    template<typename... Args>
    inline void ThrowIfFalse(BOOL result, const std::format_string<Args...> fmt, Args&&... args)
    {
        if (result == FALSE)
        {
            NEB_ASSERT(false, fmt, std::forward<Args>(args)...);
            throw HrException(ERROR_ASSERTION_FAILURE, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    inline void ThrowIfFalse(BOOL result, std::string_view message)
    {
        if (result == FALSE)
        {
            NEB_ASSERT(false, message);
            throw HrException(ERROR_ASSERTION_FAILURE, message);
        }
    }

    inline void ThrowIfFalse(BOOL result)
    {
        if (result == FALSE)
        {
            NEB_ASSERT(false);
            throw HrException(ERROR_ASSERTION_FAILURE);
        }
    }

} // Neb::nri namespace
