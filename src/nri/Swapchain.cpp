#include "Swapchain.h"

namespace Neb::nri
{

    BOOL Swapchain::Init(HWND hwnd, 
        D3D12Rc<ID3D12Device> device,
        D3D12Rc<IDXGIFactory6> dxgiFactory,
        D3D12Rc<ID3D12CommandQueue> graphicsQueue,
        DescriptorHeap* renderTargetHeap)
    {
        ThrowIfFalse(device && dxgiFactory && graphicsQueue && renderTargetHeap);
        m_device = device;
        m_dxgiFactory = dxgiFactory;
        m_graphicsQueue = graphicsQueue;
        m_renderTargetHeap = renderTargetHeap;

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

        ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
            m_graphicsQueue.Get(),
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
            m_renderTargetViews[i] = m_renderTargetHeap->AllocateDescriptor();
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews[i].DescriptorHandle);
        }

        return TRUE;
    }

}