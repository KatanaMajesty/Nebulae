#include "NRIDescriptorHeap.h"

void NRIDescriptorHeap::Init(ID3D12Device* device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type           = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags          = flags;
    desc.NodeMask       = 0;
    ThrowIfFailed( device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_DescriptorHeap.ReleaseAndGetAddressOf())) );

    m_Type = type;
    m_DescriptorHandleIncrementSize = device->GetDescriptorHandleIncrementSize(type);
}

NRIDescriptorAllocation NRIDescriptorHeap::operator[](UINT index) const
{
    return NRIDescriptorAllocation{
        .DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(GetCPUDescriptorHandleForHeapStart(), index, m_DescriptorHandleIncrementSize),
        .DescriptorIndex  = index
    };
}

NRIDescriptorAllocation NRIDescriptorHeap::AllocateDescriptor()
{
    UINT AllocationIndex = m_CurrentDesciptorIndex;
    if (!m_FreedIndices.empty())
    {
        UINT nextFreeIndex = m_FreedIndices.front();
        m_FreedIndices.pop();
        
        AllocationIndex = nextFreeIndex;
    }
    else m_CurrentDesciptorIndex++; // If we did not get the index from queue - increment current descriptor index
    
    return operator[](AllocationIndex);
}

void NRIDescriptorHeap::ReleaseDescriptor(NRIDescriptorAllocation&& alloc)
{
    if (alloc.DescriptorIndex != UINT(-1))
    {
        m_FreedIndices.push(alloc.DescriptorIndex);
    }

    alloc.DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
    alloc.DescriptorIndex  = UINT(-1);
}

D3D12_CPU_DESCRIPTOR_HANDLE NRIDescriptorHeap::GetCPUDescriptorHandleForNextIndex() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(GetCPUDescriptorHandleForHeapStart(), m_CurrentDesciptorIndex, m_DescriptorHandleIncrementSize);
}