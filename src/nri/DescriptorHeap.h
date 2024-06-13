#pragma once

#include "stdafx.h"
#include "DescriptorAllocation.h"

#include <queue>

namespace Neb::nri
{

    class DescriptorHeap
    {
    public:
        void Init(ID3D12Device* device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, D3D12_DESCRIPTOR_HEAP_FLAGS flags);

        D3D12_DESCRIPTOR_HEAP_TYPE GetType() const { return m_type; }
        ID3D12DescriptorHeap* GetHeap() { return m_descriptorHeap.Get(); }

        DescriptorAllocation operator[](UINT index) const;
        DescriptorAllocation AllocateDescriptor();
        void ReleaseDescriptor(DescriptorAllocation&& alloc);

        DescriptorRange AllocateDescriptorRange(UINT numDescriptors);
        void ReleaseDescriptorRange(DescriptorRange&& rangeAlloc);

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart() const { return m_descriptorHeap->GetCPUDescriptorHandleForHeapStart(); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForNextIndex() const;

        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart() const { return m_descriptorHeap->GetGPUDescriptorHandleForHeapStart(); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForNextIndex() const;

        D3D12_DESCRIPTOR_HEAP_TYPE m_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        D3D12Rc<ID3D12DescriptorHeap> m_descriptorHeap;

        UINT m_numDescriptors = 0;
        UINT m_currentDesciptorIndex = 0;
        UINT m_descriptorHandleIncrementSize = 0;

        std::queue<UINT> m_freedIndices;
    };

} // Neb::nri namespace