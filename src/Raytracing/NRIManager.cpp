#include "NRIManager.h"

// Chill here for getting started -> https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component

NRIManager::NRIManager(const NRICreationDesc& desc)
{
    Init(desc);
}

void NRIManager::Init(const NRICreationDesc& desc)
{
    InitPipeline(desc);
}

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component#loadpipeline
void NRIManager::InitPipeline(const NRICreationDesc& desc)
{
    // Enable the debug layer
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(m_DebugInterface.ReleaseAndGetAddressOf())));

    m_DebugInterface->SetEnableGPUBasedValidation(TRUE);
    m_DebugInterface->EnableDebugLayer();

    // Create the factory
    ComPtr<IDXGIFactory2> factory;
    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(m_DxgiFactory.ReleaseAndGetAddressOf())));
    ThrowIfFailed(factory->QueryInterface(IID_PPV_ARGS(m_DxgiFactory.ReleaseAndGetAddressOf())));

    // Query appropriate dxgi adapter
    ThrowIfFalse(QueryMostSuitableDeviceAdapter());

    ThrowIfFailed(D3D12CreateDevice(m_DxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_Device.ReleaseAndGetAddressOf())));

    // Fill out a command queue description, then create command queues
    InitCommandQueues();

    // Fill out a swapchain description, then create the swap chain
    InitSwapchain(desc.Handle, desc.NumBackbuffers);

    // Fill out a heap description. then create a descriptor heap
    // D3D12_DESCRIPTOR_HEAP_DESC
    // ID3D12Device::CreateDescriptorHeap
    // ID3D12Device::GetDescriptorHandleIncrementSize

        // Create the render target view
        // CD3DX12_CPU_DESCRIPTOR_HANDLE
        // GetCPUDescriptorHandleForHeapStart
        // IDXGISwapChain::GetBuffer
        // ID3D12Device::CreateRenderTargetView

        // Create the command allocator: ID3D12Device::CreateCommandAllocator.
}

BOOL NRIManager::IsDxgiAdapterRaytracingSupported(ComPtr<ID3D12Device> device) const
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))) ||
        featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL NRIManager::IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const
{
    // Don't select render driver, provided by D3D12. We only use physical hardware
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
    {
        return FALSE;
    }

    // Passing nullptr as a parameter would just test the adapter for compatibility
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(DxgiAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()))) ||
        !IsDxgiAdapterRaytracingSupported(device))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL NRIManager::QueryMostSuitableDeviceAdapter()
{
    DXGI_GPU_PREFERENCE preference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

    UINT64 maxVideoMemory = 0;
    IDXGIAdapter3* adapter;
    for (UINT i = 0;
        (HRESULT)m_DxgiFactory->EnumAdapterByGpuPreference(i, preference, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; // Implicit cast between different types is fine...
        ++i)
    {
        DXGI_ADAPTER_DESC1 adapterDesc;
        adapter->GetDesc1(&adapterDesc);

        // If the adapter is suitable, we can compare it with current adapter
        if (adapterDesc.DedicatedVideoMemory > maxVideoMemory && IsDxgiAdapterSuitable(adapter, adapterDesc))
        {
            maxVideoMemory = adapterDesc.DedicatedVideoMemory;
            m_DxgiAdapter.Attach(adapter);
            continue;
        }

        adapter->Release();
        adapter = nullptr;
    }

    return m_DxgiAdapter != NULL ? TRUE : FALSE;
}

void NRIManager::InitCommandQueues()
{
    D3D12_COMMAND_QUEUE_DESC graphicsQueueDesc = {};
    graphicsQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    graphicsQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    graphicsQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    graphicsQueueDesc.NodeMask = 0;
    ThrowIfFailed(m_Device->CreateCommandQueue(&graphicsQueueDesc, IID_PPV_ARGS(m_GraphicsQueue.ReleaseAndGetAddressOf())));

    D3D12_COMMAND_QUEUE_DESC copyQueueDesc = {};
    copyQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    copyQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copyQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    copyQueueDesc.NodeMask = 0;
    ThrowIfFailed(m_Device->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(m_CopyQueue.ReleaseAndGetAddressOf())));

    D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
    computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    computeQueueDesc.NodeMask = 0;
    ThrowIfFailed(m_Device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(m_ComputeQueue.ReleaseAndGetAddressOf())));
}

void NRIManager::InitSwapchain(HWND hwnd, UINT numBackbuffers)
{
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.Width = 0;
    swapchainDesc.Height = 0;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.SampleDesc.Quality = 0;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.BufferCount = numBackbuffers;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.Flags = 0 /*DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING*/;

    ThrowIfFailed(m_DxgiFactory->CreateSwapChainForHwnd(
        m_GraphicsQueue.Get(),
        hwnd,
        &swapchainDesc,
        NULL,   // Set it to NULL to create a windowed swap chain.
        NULL,   // Set this parameter to NULL if you don't want to restrict content to an output target.
        m_DxgiSwapchain.ReleaseAndGetAddressOf()
    ));
}
