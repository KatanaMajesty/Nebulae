#include "Swapchain.h"

#include "../common/Defines.h"

namespace Neb::nri
{

    BOOL Swapchain::Init(HWND hwnd, Neb::nri::Manager* nriManager)
    {
        ThrowIfFalse(nriManager != nullptr);
        m_nriManager = nriManager;

        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.Width = 0;
        swapchainDesc.Height = 0;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.BufferCount = NumBackbuffers;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.Flags = 0 /*DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING*/;

        ThrowIfFailed(m_nriManager->GetDxgiFactory()->CreateSwapChainForHwnd(
            m_nriManager->GetCommandQueue(eCommandContextType_Graphics),
            hwnd,
            &swapchainDesc,
            NULL,   // Set it to NULL to create a windowed swap chain.
            NULL,   // Set this parameter to NULL if you don't want to restrict content to an output target.
            m_dxgiSwapchain.ReleaseAndGetAddressOf()
        ));

        // Create the render target view
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed(m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].ReleaseAndGetAddressOf())));
            m_renderTargetViews[i] = m_nriManager->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptor();
            m_nriManager->GetDevice()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews[i].DescriptorHandle);
        }

        return TRUE;
    }

    BOOL Swapchain::Resize(UINT width, UINT height)
    {
        if (!ReleaseDxgiReferences())
        {
            NEB_ASSERT(false); // Failed to release references! Someone else uses them!
            return FALSE;
        }

        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // preserve the original format of the swapchain as for now
        UINT flags = 0; // DXGI_SWAP_CHAIN_FLAG type
        if (FAILED(m_dxgiSwapchain->ResizeBuffers(NumBackbuffers, width, height, format, flags)))
        {
            NEB_ASSERT(false);
            return FALSE;
        }

        // We assume that the client already issued a wait for graphics queue to finish executing all frames
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed( m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].GetAddressOf())) );
            m_nriManager->GetDevice()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews[i].DescriptorHandle);
        }
        return TRUE;
    }

    BOOL Swapchain::ReleaseDxgiReferences()
    {
        // We only release rt buffers, no need to release rtvs, as they do not affect resizing
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            // if new refcount > 0 -> bail, no need to release all of them, we will fail anyways
            if (m_renderTargets->Reset() != 0)
                return FALSE;
        }
        return TRUE;
    }

}