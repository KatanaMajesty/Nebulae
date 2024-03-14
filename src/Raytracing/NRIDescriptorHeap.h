#pragma once

#include "stdafx.h"

#include <queue>

struct NRIDescriptorAllocation
{
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle;
    UINT DescriptorIndex;
};

class NRIDescriptorHeap
{
public:
    void Init(ID3D12Device* device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);

    D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_Type; }
    ID3D12DescriptorHeap*      GetHeap() { return m_DescriptorHeap.Get(); }

    NRIDescriptorAllocation operator[](UINT index) const;
    NRIDescriptorAllocation AllocateDescriptor();
    void                    ReleaseDescriptor(NRIDescriptorAllocation&& alloc);

private:
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart() const { return m_DescriptorHeap->GetCPUDescriptorHandleForHeapStart(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForNextIndex() const;

    D3D12_DESCRIPTOR_HEAP_TYPE   m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    ComPtr<ID3D12DescriptorHeap> m_DescriptorHeap;

    UINT m_CurrentDesciptorIndex = 0;
    UINT m_DescriptorHandleIncrementSize = 0;

    std::queue<UINT> m_FreedIndices;
};