#pragma once

#include <algorithm>
#include "stdafx.h"
#include "DescriptorHeapAllocation.h"
#include "DescriptorHeap.h"

namespace Neb::nri
{

    class Swapchain
    {
    public:
        static constexpr UINT MaxBackbuffers = DXGI_MAX_SWAP_CHAIN_BUFFERS;
        static constexpr UINT NumBackbuffers = 3;
        static_assert(NumBackbuffers <= MaxBackbuffers && "This amount of backbuffers is not supported");

        Swapchain() = default;

        Swapchain(const Swapchain&) = default;
        Swapchain& operator=(const Swapchain&) = default;

        ~Swapchain();

        // Initializes the swapchain, returns true if initialized successfully
        BOOL Init(HWND hwnd);

        // Resizes all the backbuffers of the swapchain. If width == 0 && height == 0 the swapchain will
        // use the dimentions of window's client area
        BOOL Resize(UINT width = 0, UINT height = 0);
        UINT GetWidth() const { return m_swapchainDesc.BufferDesc.Width; }
        UINT GetHeight() const { return m_swapchainDesc.BufferDesc.Height; }
        DXGI_FORMAT GetFormat() const { return m_swapchainDesc.BufferDesc.Format; }

        void Present(BOOL vsync);

        IDXGISwapChain4* GetDxgiSwapchain() { return m_dxgiSwapchain.Get(); }
        UINT GetCurrentBackbufferIndex() const { return m_dxgiSwapchain->GetCurrentBackBufferIndex(); }

        ID3D12Resource* GetBackbuffer(UINT index) { return m_renderTargets[index].Get(); }
        ID3D12Resource* GetCurrentBackbuffer() { return GetBackbuffer(GetCurrentBackbufferIndex()); }
        const DescriptorHeapAllocation& GetBackbufferRtvs() const { return m_renderTargetViews; }

        D3D12_CPU_DESCRIPTOR_HANDLE GetBackbufferRtvHandle(UINT index) const { return m_renderTargetViews.CpuAt(index); }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackbufferRtvHandle() const { return m_renderTargetViews.CpuAt(GetCurrentBackbufferIndex()); }

    private:
        BOOL ReleaseDxgiReferences();

        DXGI_SWAP_CHAIN_DESC m_swapchainDesc = {};
        D3D12Rc<IDXGISwapChain4> m_dxgiSwapchain;
        D3D12Rc<ID3D12Resource> m_renderTargets[NumBackbuffers];
        DescriptorHeapAllocation m_renderTargetViews;
    };

}