#pragma once

#include "nri/stdafx.h"

namespace Neb::nri
{
    
    class NvDriver
    {
    private:
        NvDriver() = default;

    public:
        NvDriver(const NvDriver&) = delete;
        NvDriver& operator=(const NvDriver&) = delete;

        static NvDriver* Get() noexcept
        {
            static NvDriver instance;
            return &instance;
        }

        inline bool IsValid() const noexcept { return m_device != nullptr; }

        bool InitD3D12(Rc<ID3D12Device5> device);
        void ShutD3D12();

    private:
        Rc<ID3D12Device5> m_device;
        void* m_dxrMessengerHandle = nullptr;
    }; // NvDriver struct

} // Neb::nri namespace