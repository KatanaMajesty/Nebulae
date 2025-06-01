#pragma once

#include "stdafx.h"
#include <D3D12MA/D3D12MemAlloc.h>
#include "DescriptorHeapAllocation.h"

namespace Neb::nri
{

    class DepthStencilBuffer
    {
    public:
        BOOL Init(UINT width, UINT height,
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

        BOOL Resize(UINT width, UINT height);
        DXGI_FORMAT GetFormat() const { return m_desc.Format; }

        const D3D12_RESOURCE_DESC& GetDesc() const { return m_desc; }
        ID3D12Resource* GetBufferResource() const { return m_bufferAllocation->GetResource(); }

        const DescriptorHeapAllocation& GetDSV() const { return m_depthStencilView; }

    private:
        void AllocateAll();

        D3D12_RESOURCE_DESC m_desc = {};
        D3D12Rc<D3D12MA::Allocation> m_bufferAllocation;

        DescriptorHeapAllocation m_depthStencilView;
        DescriptorHeapAllocation m_depthSrv;
    };

}