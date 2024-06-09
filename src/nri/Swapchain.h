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

        IDXGISwapChain1* GetDxgiSwapchain() { return m_dxgiSwapchain.Get(); }
        DescriptorAllocation GetSwapchainRtv(UINT backbufferIndex) const { return m_renderTargetViews[backbufferIndex]; }

    private:
        BOOL ReleaseDxgiReferences();

        Neb::nri::Manager* m_nriManager;

        D3D12Rc<IDXGISwapChain1> m_dxgiSwapchain;
        D3D12Rc<ID3D12Resource> m_renderTargets[NumBackbuffers];
        DescriptorAllocation m_renderTargetViews[NumBackbuffers];
    };

}