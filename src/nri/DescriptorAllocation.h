#pragma once

#include "stdafx.h"

namespace Neb::nri
{

    struct DescriptorAllocation
    {
        // TODO: Not sure if that would ever be the problem. Dont think so - whatever
        BOOL IsValid() const { return DescriptorIndex != UINT(-1); }

        D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE();
        UINT DescriptorIndex = UINT(-1);
    };

}