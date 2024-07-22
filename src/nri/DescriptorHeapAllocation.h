#pragma once

#ifndef NEBULAE_NRI_DESCRIPTOR_HEAP_ALLOCATION_H_
#define NEBULAE_NRI_DESCRIPTOR_HEAP_ALLOCATION_H_

#include "stdafx.h"

namespace Neb::nri
{

    // Descriptor heap allocations are NOT managed
    // The underlying memory will be freed by the relevant DescriptorHeap when destroyed
    struct DescriptorHeapAllocation
    {
        BOOL IsNull() const { return CpuAddress == CD3DX12_CPU_DESCRIPTOR_HANDLE(); }
        BOOL IsShaderVisible() const { return GpuAddress != CD3DX12_GPU_DESCRIPTOR_HANDLE(); }

        D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(UINT index) const { return CD3DX12_CPU_DESCRIPTOR_HANDLE(CpuAddress, index, DescriptorIncrementSize); }
        D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(UINT index) const { return CD3DX12_GPU_DESCRIPTOR_HANDLE(GpuAddress, index, DescriptorIncrementSize); }

        CD3DX12_CPU_DESCRIPTOR_HANDLE CpuAddress = CD3DX12_CPU_DESCRIPTOR_HANDLE();
        CD3DX12_GPU_DESCRIPTOR_HANDLE GpuAddress = CD3DX12_GPU_DESCRIPTOR_HANDLE();
        UINT NumDescriptors = 0;
        UINT DescriptorIncrementSize = 0;
        UINT Index = 0;
    };

} // Neb::nri namespace

#endif // NEBULAE_NRI_DESCRIPTOR_HEAP_ALLOCATION_H_