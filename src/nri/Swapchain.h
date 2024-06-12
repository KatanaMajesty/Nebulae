#pragma once

#include <algorithm>
#include "stdafx.h"
#include "Manager.h"
#include "DescriptorAllocation.h"
#include "DescriptorHeap.h"

namespace Neb::nri
{

    class Swapchain
    {
    public:
        static constexpr UINT MaxBackbuffers = DXGI_MAX_SWAP_CHAIN_BUFFERS;
        static constexpr UINT NumBackbuffers = std::min(3u, MaxBackbuffers);

        Swapchain() = default;

        // Initializes the swapchain, returns true if initialized successfully
        BOOL Init(HWND hwnd, Neb::nri::Manager* nriManager);

        // Resizes all the backbuffers of the swapchain. If width == 0 && height == 0 the swapchain will
        // use the dimentions of window's client area 
        BOOL Resize(UINT width = 0, UINT height = 0);
        UINT GetWidth() const { return m_swapchainDesc.BufferDesc.Width; }
        UINT GetHeight() const { return m_swapchainDesc.BufferDesc.Height; }

        void Present(BOOL vsync);

        IDXGISwapChain4* GetDxgiSwapchain() { return m_dxgiSwapchain.Get(); }
        UINT GetCurrentBackbufferIndex() const { return m_dxgiSwapchain->GetCurrentBackBufferIndex(); }

        ID3D12Resource* GetBackbuffer(UINT index) { return m_renderTargets[index].Get(); }
        ID3D12Resource* GetCurrentBackbuffer() { return GetBackbuffer(GetCurrentBackbufferIndex()); }
        DescriptorAllocation GetBackbufferRtv(UINT index) const { return m_renderTargetViews[index]; }
        DescriptorAllocation GetCurrentBackbufferRtv() const { return GetBackbufferRtv(GetCurrentBackbufferIndex()); }

    private:
        BOOL ReleaseDxgiReferences();

        Neb::nri::Manager* m_nriManager;

        DXGI_SWAP_CHAIN_DESC m_swapchainDesc;
        D3D12Rc<IDXGISwapChain4> m_dxgiSwapchain;
        D3D12Rc<ID3D12Resource> m_renderTargets[NumBackbuffers];
        DescriptorAllocation m_renderTargetViews[NumBackbuffers];
    };

}