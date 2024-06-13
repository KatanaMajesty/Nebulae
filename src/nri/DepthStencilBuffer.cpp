#include "DepthStencilBuffer.h"

#include "../common/Defines.h"
#include "Manager.h"

namespace Neb::nri
{

    BOOL DepthStencilBuffer::Init(UINT width, UINT height)
    {
        Manager& nriManager = Manager::Get();
        
        m_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R24G8_TYPELESS, width, height, 1, 1);
        m_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        m_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        m_depthStencilView = nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).AllocateDescriptor();
        AllocateAll();
        return TRUE;
    }

    BOOL DepthStencilBuffer::Resize(UINT width, UINT height)
    {
        if (width == m_desc.Width && height == m_desc.Height)
        {
            // Nothing to do, avoid resizing
            return TRUE;
        }

        if (!m_bufferAllocation)
            return FALSE;

        m_desc.Width = width;
        m_desc.Height = height;
        AllocateAll();

        return TRUE;
    }

    void DepthStencilBuffer::AllocateAll()
    {
        Manager& nriManager = Manager::Get();
        D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = D3D12MA::ALLOCATION_DESC{
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT,
        };

        CD3DX12_CLEAR_VALUE optimizedClearValue(DXGI_FORMAT_D24_UNORM_S8_UINT, 1.0f, 0);
        ThrowIfFailed(allocator->CreateResource(
            &allocDesc,
            &m_desc,
            D3D12_RESOURCE_STATE_COMMON,
            &optimizedClearValue, m_bufferAllocation.ReleaseAndGetAddressOf(),
            __uuidof(nullptr), nullptr
        ));

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = D3D12_DEPTH_STENCIL_VIEW_DESC{
            .Format = DXGI_FORMAT_D24_UNORM_S8_UINT,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D = D3D12_TEX2D_DSV{.MipSlice = 0 }
        };
        nriManager.GetDevice()->CreateDepthStencilView(m_bufferAllocation->GetResource(), &dsvDesc, m_depthStencilView.DescriptorHandle);
    }

}