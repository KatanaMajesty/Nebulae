#pragma once

#include "stdafx.h"

namespace Neb::nri
{

    // TODO: Needs refactoring, not suitable for GPU descriptors
    struct DescriptorAllocation
    {
        // TODO: Not sure if that would ever be the problem. Dont think so - whatever
        BOOL IsValid() const { return DescriptorIndex != UINT(-1); }

        D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
        UINT DescriptorIndex = UINT(-1);
    };

    // Not to break old code, adding range type to allow allocation of several descriptors
    // and guaranteeing they are continuous
    struct DescriptorRange
    {
        inline constexpr bool IsValid() const { return NumDescriptors > 0 && DescriptorIndex != UINT(-1) && !IsNull(); }
        inline constexpr bool IsNull() const { return CPUBeginHandle.ptr == 0; }
        inline constexpr bool IsShaderVisible() const { return GPUBeginHandle.ptr != 0; }
        inline constexpr operator bool() const { return !IsNull(); }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UINT index = 0) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle(UINT index = 0) const;

        D3D12_CPU_DESCRIPTOR_HANDLE CPUBeginHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
        D3D12_GPU_DESCRIPTOR_HANDLE GPUBeginHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE();
        UINT NumDescriptors = 0;
        UINT DescriptorIndex = UINT(-1);
        UINT DescriptorIncrementSize = 0;
    };

}