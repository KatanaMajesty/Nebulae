#pragma once

#include "nri/stdafx.h"
#include "nri/Device.h"
#include "nri/DescriptorHeapAllocation.h"

namespace Neb::nri
{

    class RtInstanceDescriptorMap
    {
    public:
        RtInstanceDescriptorMap() = default;
        RtInstanceDescriptorMap(NRIDevice* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors);

        template<typename T>
        void AddInstance(UINT instanceID, Rc<ID3D12Resource> resource, const T&)
        {
            ThrowIfFalse(false, "no implementation for this descriptor desc");
        }

        template<>
        void AddInstance(UINT instanceID, Rc<ID3D12Resource> resource, const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc)
        {
            m_device->GetD3D12Device()->CreateShaderResourceView(resource.Get(), &srvDesc, m_descriptorAllocation.CpuAt(instanceID));
        }

    private:
        NRIDevice* m_device = nullptr;
        DescriptorHeapAllocation m_descriptorAllocation;
    };

}; // Neb::nri namespace