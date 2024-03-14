#pragma once

#include "stdafx.h"
#include <D3D12MA/D3D12MemAlloc.h>

#include "NRIDescriptorHeap.h"

struct NRITraits
{
    static constexpr UINT NumBackbuffers = 3;
};

struct NRICreationDesc
{
    HWND Handle = NULL;
};

// Nebulae Rendering Interface manager -> All-in-One class for D3D12 stuff management
class NRIManager
{
public:
    static constexpr UINT NumBackbuffers = 3;

    NRIManager(const NRICreationDesc& desc);

    // General D3D12-related calls
    ID3D12Device* GetDevice() { return m_Device.Get(); }
    
    ID3D12CommandQueue* GetGraphicsQueue()  { return m_GraphicsQueue.Get(); }
    ID3D12CommandQueue* GetCopyQueue()      { return m_CopyQueue.Get(); }
    ID3D12CommandQueue* GetComputeQueue()   { return m_ComputeQueue.Get(); }
    
    ID3D12CommandAllocator* GetGraphicsCommandAllocator() { return m_GraphicsCommandAllocator.Get(); }
    ID3D12CommandAllocator* GetCopyCommandAllocator()     { return m_CopyCommandAllocator.Get(); }
    ID3D12CommandAllocator* GetComputeCommandAllocator()  { return m_GraphicsCommandAllocator.Get(); }
    
    NRIDescriptorHeap*  GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) { return &m_DescriptorHeaps[type]; }

    // Swapchain-related calls
    IDXGISwapChain1* GetDxgiSwapchain() { return m_DxgiSwapchain.Get(); }
    NRIDescriptorAllocation GetSwapchainRtv(UINT backbufferIndex) const { return m_RenderTargetViews[backbufferIndex]; }

    // Resource-management calls
    D3D12MA::Allocator* GetResourceAllocator() { return m_D3D12Allocator.Get(); }

private:
    // All-in-One initialization of D3D12 stuff
    // Below these init functions goes raw D3D12 functions
    void Init(const NRICreationDesc& desc);
    void InitPipeline(const NRICreationDesc& desc);

    ComPtr<ID3D12Debug1>  m_DebugInterface;
    ComPtr<IDXGIFactory6> m_DxgiFactory;

    // Most suitable adapter for device creation
    BOOL IsDxgiAdapterMeshShaderSupported(ComPtr<ID3D12Device> device) const;
    BOOL IsDxgiAdapterRaytracingSupported(ComPtr<ID3D12Device> device) const;
    BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
    BOOL QueryMostSuitableDeviceAdapter();
    ComPtr<IDXGIAdapter3> m_DxgiAdapter;
    ComPtr<ID3D12Device>  m_Device;

    void InitCommandQueuesAndAllocators();
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_ComputeQueue;
    ComPtr<ID3D12CommandAllocator> m_GraphicsCommandAllocator;
    ComPtr<ID3D12CommandAllocator> m_CopyCommandAllocator;
    ComPtr<ID3D12CommandAllocator> m_ComputeCommandAllocator;

    void InitDescriptorHeaps();
    NRIDescriptorHeap m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

    void InitSwapchain(HWND hwnd);
    ComPtr<IDXGISwapChain1> m_DxgiSwapchain;
    ComPtr<ID3D12Resource>  m_RenderTargets    [NumBackbuffers];
    NRIDescriptorAllocation m_RenderTargetViews[NumBackbuffers];

    void InitResourceAllocator();
    ComPtr<D3D12MA::Allocator> m_D3D12Allocator;
};