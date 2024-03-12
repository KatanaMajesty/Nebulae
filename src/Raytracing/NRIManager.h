#pragma once

#include "stdafx.h"
#include <D3D12MA/D3D12MemAlloc.h>

struct NRITraits
{
    static constexpr UINT NumBackbuffers = 3;
};

struct NRICreationDesc
{
    HWND Handle = NULL;
    UINT NumBackbuffers = NRITraits::NumBackbuffers;
};

// Nebulae Rendering Interface manager -> All-in-One class for D3D12 stuff management
class NRIManager
{
public:
    NRIManager(const NRICreationDesc& desc);

    // General D3D12-related calls
    ID3D12Device* GetDevice() { return m_Device.Get(); }
    ID3D12CommandQueue* GetGraphicsQueue()  { return m_GraphicsQueue.Get(); }
    ID3D12CommandQueue* GetCopyQueue()      { return m_CopyQueue.Get(); }
    ID3D12CommandQueue* GetComputeQueue()   { return m_ComputeQueue.Get(); }
    
    // Swapchain-related calls
    IDXGISwapChain1* GetDxgiSwapchain() { return m_DxgiSwapchain.Get(); }

    // Resource-management calls
    D3D12MA::Allocator* GetAllocator() { return m_D3D12Allocator.Get(); }

private:
    // All-in-One initialization of D3D12 stuff
    // Below these init functions goes raw D3D12 functions
    void Init(const NRICreationDesc& desc);
    void InitPipeline(const NRICreationDesc& desc);

    ComPtr<ID3D12Debug1>  m_DebugInterface;
    ComPtr<IDXGIFactory6> m_DxgiFactory;

    // Most suitable adapter for device creation
    BOOL IsDxgiAdapterRaytracingSupported(ComPtr<ID3D12Device> device) const;
    BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
    BOOL QueryMostSuitableDeviceAdapter();
    ComPtr<IDXGIAdapter3> m_DxgiAdapter;
    ComPtr<ID3D12Device>  m_Device;

    void InitCommandQueues();
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_ComputeQueue;

    void InitSwapchain(HWND hwnd, UINT numBackbuffers);
    ComPtr<IDXGISwapChain1>    m_DxgiSwapchain;

    ComPtr<D3D12MA::Allocator> m_D3D12Allocator;
};