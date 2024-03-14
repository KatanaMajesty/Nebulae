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
    ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(factory.GetAddressOf())));
    ThrowIfFailed(factory->QueryInterface(IID_PPV_ARGS(m_DxgiFactory.ReleaseAndGetAddressOf())));

    // Query appropriate dxgi adapter
    ThrowIfFalse(QueryMostSuitableDeviceAdapter());

    ThrowIfFailed(D3D12CreateDevice(m_DxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_Device.ReleaseAndGetAddressOf())));

    // Fill out a command queue description, then create command queues
    InitCommandQueuesAndAllocators();

    // Fill out a heap description. then create a descriptor heap
    InitDescriptorHeaps();

    // Fill out a swapchain description, then create the swap chain
    InitSwapchain(desc.Handle);

    // Create the render target view
    for (UINT i = 0; i < NumBackbuffers; ++i)
    {
        ThrowIfFailed( m_DxgiSwapchain->GetBuffer(i, IID_PPV_ARGS(m_RenderTargets[i].ReleaseAndGetAddressOf())) );
        m_RenderTargetViews[i] = m_DescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].AllocateDescriptor();
        m_Device->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, m_RenderTargetViews[i].DescriptorHandle);
    }

    InitResourceAllocator();
}

BOOL NRIManager::IsDxgiAdapterMeshShaderSupported(ComPtr<ID3D12Device> device) const
{
    // https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#checkfeaturesupport
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureSupportData = {};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureSupportData, sizeof(featureSupportData))) ||
        featureSupportData.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
    {
        return FALSE;
    }

    return TRUE;
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
        !IsDxgiAdapterRaytracingSupported(device) ||
        !IsDxgiAdapterMeshShaderSupported(device))
    {
        return FALSE;
    }

    return TRUE;
}

BOOL NRIManager::QueryMostSuitableDeviceAdapter()
{
    UINT64 maxVideoMemory = 0;
    IDXGIAdapter3* adapter;
    for (UINT i = 0;
        m_DxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
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

void NRIManager::InitCommandQueuesAndAllocators()
{
    D3D12_COMMAND_QUEUE_DESC graphicsQueueDesc = {};
    graphicsQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    graphicsQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    graphicsQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    graphicsQueueDesc.NodeMask = 0;
    ThrowIfFailed( m_Device->CreateCommandQueue(&graphicsQueueDesc, IID_PPV_ARGS(m_GraphicsQueue.ReleaseAndGetAddressOf())) );

    D3D12_COMMAND_QUEUE_DESC copyQueueDesc = {};
    copyQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    copyQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copyQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    copyQueueDesc.NodeMask = 0;
    ThrowIfFailed( m_Device->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(m_CopyQueue.ReleaseAndGetAddressOf())) );

    D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
    computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    computeQueueDesc.NodeMask = 0;
    ThrowIfFailed( m_Device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(m_ComputeQueue.ReleaseAndGetAddressOf())) );

    // Create command allocators for each type
    ThrowIfFailed( m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,  IID_PPV_ARGS(m_GraphicsCommandAllocator.ReleaseAndGetAddressOf())) );
    ThrowIfFailed( m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,    IID_PPV_ARGS(m_GraphicsCommandAllocator.ReleaseAndGetAddressOf())) );
    ThrowIfFailed( m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(m_GraphicsCommandAllocator.ReleaseAndGetAddressOf())) );
}

void NRIManager::InitDescriptorHeaps()
{
    static constexpr UINT NumDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = 
    {
        4096, // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        256,  // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        2048, // D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        256,  // D3D12_DESCRIPTOR_HEAP_TYPE_DSV
    };

    static constexpr D3D12_DESCRIPTOR_HEAP_FLAGS Flags[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = 
    {
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE,           // D3D12_DESCRIPTOR_HEAP_TYPE_RTV
        D3D12_DESCRIPTOR_HEAP_FLAG_NONE,           // D3D12_DESCRIPTOR_HEAP_TYPE_DSV
    };

    for (UINT i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        D3D12_DESCRIPTOR_HEAP_TYPE type = (D3D12_DESCRIPTOR_HEAP_TYPE)i;
        m_DescriptorHeaps[i].Init(GetDevice(), NumDescriptors[type], type, Flags[type]);
    }
}

void NRIManager::InitSwapchain(HWND hwnd)
{
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

    ThrowIfFailed(m_DxgiFactory->CreateSwapChainForHwnd(
        m_GraphicsQueue.Get(),
        hwnd,
        &swapchainDesc,
        NULL,   // Set it to NULL to create a windowed swap chain.
        NULL,   // Set this parameter to NULL if you don't want to restrict content to an output target.
        m_DxgiSwapchain.ReleaseAndGetAddressOf()
    ));
}

void NRIManager::InitResourceAllocator()
{
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED;
    desc.pDevice = m_Device.Get();;
    desc.PreferredBlockSize = 0;
    desc.pAllocationCallbacks = nullptr;
    desc.pAdapter = m_DxgiAdapter.Get();
    ThrowIfFailed( D3D12MA::CreateAllocator(&desc, m_D3D12Allocator.ReleaseAndGetAddressOf()) );
}
