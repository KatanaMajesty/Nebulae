#include "Swapchain.h"

#include "../common/Assert.h"
#include "Device.h"

namespace Neb::nri
{
    
    Swapchain::~Swapchain()
    {
        this->Shutdown();
    }

    BOOL Swapchain::Init(HWND hwnd)
    {
        NRIDevice& device = NRIDevice::Get();

        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.Width = 0;
        swapchainDesc.Height = 0;
        swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.SampleDesc.Quality = 0;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.BufferCount = NumBackbuffers;
        swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapchainDesc.Flags = 0;

        // Recommended to always allow tearing if supported
        if (device.GetCapabilities().IsScreenTearingSupported)
            swapchainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        D3D12Rc<IDXGISwapChain1> swapchain;
        ThrowIfFailed(device.GetDxgiFactory()->CreateSwapChainForHwnd(
            device.GetCommandQueue(eCommandContextType_Graphics),
            hwnd,
            &swapchainDesc,
            NULL, // Set it to NULL to create a windowed swap chain.
            NULL, // Set this parameter to NULL if you don't want to restrict content to an output target.
            swapchain.GetAddressOf()));
        m_dxgiSwapchain.Reset();
        ThrowIfFailed(swapchain.As(&m_dxgiSwapchain));

        // Create the render target view
        m_renderTargetViews = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptors(NumBackbuffers);
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed(m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].ReleaseAndGetAddressOf())));
            NEB_SET_HANDLE_NAME(m_renderTargets[i], "Swapchain backbuffer {}", i);

            device.GetD3D12Device()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews.CpuAt(i));
        }

        // Get description of swapchain to easily obtain dimensions
        ThrowIfFailed(m_dxgiSwapchain->GetDesc(&m_swapchainDesc));
        return TRUE;
    }

    void Swapchain::Shutdown()
    {
        ThrowIfFalse(ReleaseDxgiReferences());
    }

    BOOL Swapchain::Resize(UINT width, UINT height)
    {
        if (!ReleaseDxgiReferences())
        {
            NEB_ASSERT(false, "Failed to release swapchain references! Someone uses them");
            return FALSE;
        }

        NRIDevice& device = NRIDevice::Get();

        UINT flags = 0; // DXGI_SWAP_CHAIN_FLAG type
        if (device.GetCapabilities().IsScreenTearingSupported)
            flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // preserve the original format of the swapchain as for now
        if (FAILED(m_dxgiSwapchain->ResizeBuffers(NumBackbuffers, width, height, format, flags)))
        {
            NEB_ASSERT(false, "Could not resize DXGISwapchain buffers");
            return FALSE;
        }

        // We assume that the client already issued a wait for graphics queue to finish executing all frames
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            ThrowIfFailed(m_dxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_renderTargets[i].GetAddressOf())));
            NEB_SET_HANDLE_NAME(m_renderTargets[i], "Swapchain backbuffer {}", i);

            device.GetD3D12Device()->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, m_renderTargetViews.CpuAt(i));
        }

        // Get description of swapchain to easily obtain dimensions
        ThrowIfFailed(m_dxgiSwapchain->GetDesc(&m_swapchainDesc));
        return TRUE;
    }

    void Swapchain::Present(BOOL vsync)
    {
        UINT syncInterval = vsync ? 1 : 0;
        UINT flags = 0;
        if (syncInterval == 0 && NRIDevice::Get().GetCapabilities().IsScreenTearingSupported)
            flags |= DXGI_PRESENT_ALLOW_TEARING;

        nri::ThrowIfFailed(m_dxgiSwapchain->Present(syncInterval, flags));
    }

    BOOL Swapchain::ReleaseDxgiReferences()
    {
        // We only release rt buffers, no need to release rtvs, as they do not affect resizing
        for (UINT i = 0; i < NumBackbuffers; ++i)
        {
            if (m_renderTargets[i].Reset() != 0)
            {
                NEB_LOG_WARN("Reference count should be 0 when releasing");
                //return FALSE;
            }
        }

        return TRUE;
    }

}