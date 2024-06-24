#include "Swapchain.h"

#include "../common/Defines.h"

namespace Neb::nri
{

    BOOL Swapchain::Init(HWND hwnd)
    {
        nri::Manager& nriManager = nri::Manager::Get();

        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.Width = 0;
        swapchainDesc.Height = 0;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.BufferCount = NumBackbuffers;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        D3D12Rc<IDXGISwapChain1> swapchain;
        ThrowIfFailed(nriManager.GetDxgiFactory()->CreateSwapChainForHwnd(
            nriManager.GetCommandQueue(eCommandContextType_Graphics),
            hwnd,
            &swapchainDesc,
            NULL,   // Set it to NULL to create a windowed swap chain.
            NULL,   // Set this parameter to NULL if you don't want to restrict content to an output target.
            swapchain.GetAddressOf()
        ));
        m_dxgiSwapchain.Reset();
        ThrowIfFailed(swapchain.As(&m_dxgiSwapchain));

        // Create the render target view
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed(m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].ReleaseAndGetAddressOf())));
            m_renderTargetViews[i] = nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptor();
            nriManager.GetDevice()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews[i].DescriptorHandle);
        }

        // Get description of swapchain to easily obtain dimensions
        ThrowIfFailed(m_dxgiSwapchain->GetDesc(&m_swapchainDesc));
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
        UINT flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // DXGI_SWAP_CHAIN_FLAG type
        if (FAILED(m_dxgiSwapchain->ResizeBuffers(NumBackbuffers, width, height, format, flags)))
        {
            NEB_ASSERT(false);
            return FALSE;
        }

        nri::Manager& nriManager = nri::Manager::Get();

        // We assume that the client already issued a wait for graphics queue to finish executing all frames
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed( m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].GetAddressOf())) );
            nriManager.GetDevice()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews[i].DescriptorHandle);
        }

        // Get description of swapchain to easily obtain dimensions
        ThrowIfFailed(m_dxgiSwapchain->GetDesc(&m_swapchainDesc));
        return TRUE;
    }

    void Swapchain::Present(BOOL vsync)
    {
        UINT syncInterval = vsync ? 1 : 0;
        UINT flags = 0;
        if (syncInterval == 0)
            flags |= DXGI_PRESENT_ALLOW_TEARING;

        nri::ThrowIfFailed(m_dxgiSwapchain->Present(syncInterval, flags));
    }

    BOOL Swapchain::ReleaseDxgiReferences()
    {
        // We only release rt buffers, no need to release rtvs, as they do not affect resizing
        for (UINT i = 0; i < NumBackbuffers; ++i)
            m_renderTargets[i].Reset();

        return TRUE;
    }

}