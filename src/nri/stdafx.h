#pragma once

#include <stdexcept>

#include "../Win.h"

#include <wrl/client.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <d3dx12.h>

namespace Neb::nri
{

    template<typename T>
    using D3D12Rc = Microsoft::WRL::ComPtr<T>;

    template<typename T>
    using Rc = D3D12Rc<T>; // just a shorter alias

    class HrException : public std::runtime_error
    {
    public:
        HrException(HRESULT hr)
            : std::runtime_error(HrToString(hr))
            , m_hr(hr)
        {
        }

        HRESULT Error() const { return m_hr; }

    private:
        std::string HrToString(HRESULT hr)
        {
            char s_str[64] = {};
            sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
            return s_str;
        }

        const HRESULT m_hr;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw HrException(hr);
        }
    }

    inline void ThrowIfFalse(BOOL r)
    {
        if (r == FALSE)
        {
            throw HrException(E_FAIL);
        }
    }

} // Neb::nri namespace
