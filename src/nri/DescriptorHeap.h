#pragma once

#include "DescriptorHeapAllocation.h"
#include "stdafx.h"

namespace Neb::nri
{

    class DescriptorHeap
    {
    public:
        void Init(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC& );

        const D3D12_DESCRIPTOR_HEAP_DESC& GetDesc() const { return m_desc; }
        ID3D12DescriptorHeap* GetHeap() { return m_heap.Get(); }

        DescriptorHeapAllocation AllocateDescriptors(UINT numDescriptors);

    private:
        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStart() const { return m_heap->GetCPUDescriptorHandleForHeapStart(); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStart() const { return m_heap->GetGPUDescriptorHandleForHeapStart(); }

        D3D12_DESCRIPTOR_HEAP_DESC m_desc = {};
        D3D12Rc<ID3D12DescriptorHeap> m_heap;
        UINT m_incrementSize = 0;
        UINT m_nextDescriptorIndex = 0;
    };

} // Neb::nri namespace