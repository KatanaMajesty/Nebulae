#include "DescriptorHeap.h"

#include "../common/Assert.h"
#include "../common/Log.h"

namespace Neb::nri
{

    void DescriptorHeap::Init(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& desc)
    {
        ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_heap.ReleaseAndGetAddressOf())));
        m_desc = desc;
        m_incrementSize = device->GetDescriptorHandleIncrementSize(desc.Type);
    }

    DescriptorHeapAllocation DescriptorHeap::AllocateDescriptors(UINT numDescriptors)
    {
        UINT beginIndex = m_nextDescriptorIndex;
        UINT nextIndex = m_nextDescriptorIndex + numDescriptors;
        if (nextIndex >= m_desc.NumDescriptors)
        {
            NEB_LOG_ERROR("Could not allocate space for {} descriptors", numDescriptors);
            return DescriptorHeapAllocation();
        }

        m_nextDescriptorIndex = nextIndex; // If we did not get the index from queue - increment current descriptor index
        DescriptorHeapAllocation allocation = {
            .CpuAddress = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_heap->GetCPUDescriptorHandleForHeapStart(), beginIndex, m_incrementSize),
            .NumDescriptors = numDescriptors,
            .DescriptorIncrementSize = m_incrementSize,
            .Index = beginIndex,
        };

        // Only assign Gpu address to shader visible descriptors
        if (m_desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
            m_desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
            allocation.GpuAddress = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_heap->GetGPUDescriptorHandleForHeapStart(), beginIndex, m_incrementSize);

        return allocation;
    }

} // Neb::nri namespace
