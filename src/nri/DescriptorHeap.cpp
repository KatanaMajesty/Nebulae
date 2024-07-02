#include "DescriptorHeap.h"

#include "../common/Assert.h"

namespace Neb::nri
{

    void DescriptorHeap::Init(ID3D12Device* device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = type;
        desc.NumDescriptors = numDescriptors;
        desc.Flags = flags;
        desc.NodeMask = 0;
        ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_descriptorHeap.ReleaseAndGetAddressOf())));

        m_type = type;
        m_descriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(type);

        m_numDescriptors = numDescriptors;
    }

    DescriptorAllocation DescriptorHeap::operator[](UINT index) const
    {
        // If you are here then its time to fix bound checks... Were too lazy
        NEB_ASSERT(index < m_numDescriptors, "Out of bounds");

        return DescriptorAllocation{
            .DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(GetCPUDescriptorHandleForHeapStart(), index, m_descriptorHandleIncrementSize),
            .DescriptorIndex = index
        };
    }

    DescriptorAllocation DescriptorHeap::AllocateDescriptor()
    {
        UINT AllocationIndex = m_currentDesciptorIndex;
        if (!m_freedIndices.empty())
        {
            UINT nextFreeIndex = m_freedIndices.front();
            m_freedIndices.pop();

            AllocationIndex = nextFreeIndex;
        }
        else
            m_currentDesciptorIndex++; // If we did not get the index from queue - increment current descriptor index

        return operator[](AllocationIndex);
    }

    void DescriptorHeap::ReleaseDescriptor(DescriptorAllocation&& alloc)
    {
        if (alloc.IsValid())
            m_freedIndices.push(alloc.DescriptorIndex);

        alloc.DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
        alloc.DescriptorIndex = UINT(-1);
    }

    DescriptorRange DescriptorHeap::AllocateDescriptorRange(UINT numDescriptors)
    {
        // We could optimize allocating descriptor range, but I dont want to bother
        // At least for now we ignore freed indices here - just allocate numDescriptors if able to
        if (m_currentDesciptorIndex + numDescriptors > m_numDescriptors)
        {
            return DescriptorRange();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE beginCPU = GetCPUDescriptorHandleForNextIndex();
        D3D12_GPU_DESCRIPTOR_HANDLE beginGPU = GetGPUDescriptorHandleForNextIndex();
        m_currentDesciptorIndex += numDescriptors;

        return DescriptorRange{
            .CPUBeginHandle = beginCPU,
            .GPUBeginHandle = beginGPU,
            .NumDescriptors = numDescriptors,
            .DescriptorIndex = m_currentDesciptorIndex,
            .DescriptorIncrementSize = m_descriptorHandleIncrementSize,
        };
    }

    void DescriptorHeap::ReleaseDescriptorRange(DescriptorRange&& rangeAlloc)
    {
        if (rangeAlloc.IsValid())
            for (UINT i = rangeAlloc.DescriptorIndex; i < rangeAlloc.DescriptorIndex + rangeAlloc.NumDescriptors; ++i)
                m_freedIndices.push(i);

        rangeAlloc = DescriptorRange();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCPUDescriptorHandleForNextIndex() const
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(GetCPUDescriptorHandleForHeapStart(), m_currentDesciptorIndex, m_descriptorHandleIncrementSize);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetGPUDescriptorHandleForNextIndex() const
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(GetGPUDescriptorHandleForHeapStart(), m_currentDesciptorIndex, m_descriptorHandleIncrementSize);
    }

} // Neb::nri namespace
