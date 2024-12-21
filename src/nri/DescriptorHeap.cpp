#include "DescriptorHeap.h"

#include "../common/Assert.h"
#include "../common/Log.h"

#include <type_traits>

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

    template<typename T, typename... Types>
    static constexpr bool IsAnyOf = (std::is_same_v<T, Types> || ...);

    template<typename DescriptorHandle>
    static bool IsOOBHandle(DescriptorHandle begin, DescriptorHandle end, DescriptorHandle handle)
    {
        return (handle.ptr < begin.ptr || handle.ptr >= end.ptr);
    }

    template<typename DescriptorHandle>
    static bool IsValidHandle(DescriptorHandle begin, DescriptorHandle end, DescriptorHandle handle, UINT descriptorSize)
    {
        static_assert(IsAnyOf<DescriptorHandle, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE>);

        const bool isInBounds = !IsOOBHandle(begin, end, handle);
        const bool correctAlignment = !static_cast<bool>((end.ptr - handle.ptr) % descriptorSize);
        return isInBounds && correctAlignment;
    }

    bool DescriptorHeap::IsValidCPUDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE beginAddress = GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE endAddress = CD3DX12_CPU_DESCRIPTOR_HANDLE(beginAddress, m_desc.NumDescriptors, m_incrementSize);
        return IsValidHandle(beginAddress, endAddress, cpuHandle, m_incrementSize);
    }

    bool DescriptorHeap::IsValidGPUDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE beginAddress = GetGPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE endAddress = CD3DX12_GPU_DESCRIPTOR_HANDLE(beginAddress, m_desc.NumDescriptors, m_incrementSize);
        return IsValidHandle(beginAddress, endAddress, gpuHandle, m_incrementSize);
    }

} // Neb::nri namespace
