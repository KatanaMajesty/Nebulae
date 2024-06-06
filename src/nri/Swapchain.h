#pragma once

#include "stdafx.h"
#include "DescriptorAllocation.h"
#include "DescriptorHeap.h"

namespace Neb::nri
{

    class Swapchain
    {
    public:
        static constexpr UINT NumBackbuffers = 3;

        Swapchain() = default;

        // Initializes the swapchain, returns true if initialized successfully
        BOOL Init(HWND hwnd, 
            D3D12Rc<ID3D12Device> device, 
            D3D12Rc<IDXGIFactory6> dxgiFactory, 
            D3D12Rc<ID3D12CommandQueue> graphicsQueue, 
            DescriptorHeap* renderTargetHeap);

        IDXGISwapChain1* GetDxgiSwapchain() { return m_dxgiSwapchain.Get(); }
        DescriptorAllocation GetSwapchainRtv(UINT backbufferIndex) const { return m_renderTargetViews[backbufferIndex]; }

    private:
        D3D12Rc<ID3D12Device> m_device;
        D3D12Rc<IDXGIFactory6> m_dxgiFactory;
        D3D12Rc<ID3D12CommandQueue> m_graphicsQueue;

        D3D12Rc<IDXGISwapChain1> m_dxgiSwapchain;
        D3D12Rc<ID3D12Resource> m_renderTargets[NumBackbuffers];

        DescriptorHeap* m_renderTargetHeap = nullptr;
        DescriptorAllocation m_renderTargetViews[NumBackbuffers];
    };

}