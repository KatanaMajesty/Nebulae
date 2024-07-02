#pragma once

#include <vector>
#include "stdafx.h"
#include "D3D12MA/D3D12MemAlloc.h"
#include "DescriptorAllocation.h"

namespace Neb::nri
{

    struct ConstantBufferRangeDesc
    {
        D3D12MA::ALLOCATION_DESC AllocDesc = {};
        D3D12_RESOURCE_STATES InitialStates = D3D12_RESOURCE_STATE_GENERIC_READ;
        SIZE_T NumBuffersInRange = 0;
        SIZE_T NumBytesPerBuffer = 0;
    };

    class ConstantBufferRange
    {
    public:
        BOOL Init(const ConstantBufferRangeDesc& desc) noexcept;

        LPVOID GetMapping(UINT frameIndex) const noexcept { return m_mappings.at(frameIndex); }

    private:
        // This allocation stores one buffer resource, which size is = NumBuffersInRange * NumBytesPerBuffer,
        // which would avoid multiple allocations at once
        D3D12Rc<D3D12MA::Allocation> m_bufferRangeAllocation;

        // An array of mapping will contain NumBuffersInRange mappings in total, which can almost always be assumed
        // and will be checked by internal API
        std::vector<LPVOID> m_mappings;

        // m_descriptor range stores all NumBuffersInRange CBV descriptors in a single range allocation
        DescriptorRange m_descriptor;
    };

} // Neb::nri namespace