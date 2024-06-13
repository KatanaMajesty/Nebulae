#include "DescriptorAllocation.h"

namespace Neb::nri
{
    
    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRange::GetCPUHandle(UINT index) const
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(CPUBeginHandle, DescriptorIncrementSize, index);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRange::GetGPUHandle(UINT index) const
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(GPUBeginHandle, DescriptorIncrementSize, index);
    }

} // Neb::nri namespace