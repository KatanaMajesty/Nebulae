#include "Manager.h"

// Chill here for getting started -> https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component

namespace Neb::nri
{

    void Manager::Init()
    {
        InitPipeline();
    }

    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component#loadpipeline
    void Manager::InitPipeline()
    {
        // Enable the debug layer
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(m_debugInterface.ReleaseAndGetAddressOf())));

        m_debugInterface->SetEnableGPUBasedValidation(TRUE);
        m_debugInterface->EnableDebugLayer();

        // Create the factory
        D3D12Rc<IDXGIFactory2> factory;
        ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(factory.GetAddressOf())));
        ThrowIfFailed(factory->QueryInterface(IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

        // Query appropriate dxgi adapter
        ThrowIfFalse(QueryMostSuitableDeviceAdapter());

        ThrowIfFailed(D3D12CreateDevice(m_dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf())));

        // Fill out a command queue description, then create command queues
        InitCommandQueuesAndAllocators();

        // Fill out a heap description. then create a descriptor heap
        InitDescriptorHeaps();

        InitResourceAllocator();
    }

    BOOL Manager::IsDxgiAdapterMeshShaderSupported(D3D12Rc<ID3D12Device> device) const
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

    BOOL Manager::IsDxgiAdapterRaytracingSupported(D3D12Rc<ID3D12Device> device) const
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))) ||
            featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
        {
            return FALSE;
        }

        return TRUE;
    }

    BOOL Manager::IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const
    {
        // Don't select render driver, provided by D3D12. We only use physical hardware
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            return FALSE;
        }

        // Passing nullptr as a parameter would just test the adapter for compatibility
        D3D12Rc<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(DxgiAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()))) ||
            !IsDxgiAdapterRaytracingSupported(device) ||
            !IsDxgiAdapterMeshShaderSupported(device))
        {
            return FALSE;
        }

        return TRUE;
    }

    BOOL Manager::QueryMostSuitableDeviceAdapter()
    {
        UINT64 maxVideoMemory = 0;
        IDXGIAdapter3* adapter;
        for (UINT i = 0;
            m_dxgiFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
            ++i)
        {
            DXGI_ADAPTER_DESC1 adapterDesc;
            adapter->GetDesc1(&adapterDesc);

            // If the adapter is suitable, we can compare it with current adapter
            if (adapterDesc.DedicatedVideoMemory > maxVideoMemory && IsDxgiAdapterSuitable(adapter, adapterDesc))
            {
                maxVideoMemory = adapterDesc.DedicatedVideoMemory;
                m_dxgiAdapter.Attach(adapter);
                continue;
            }

            adapter->Release();
            adapter = nullptr;
        }

        return m_dxgiAdapter != NULL ? TRUE : FALSE;
    }

    void Manager::InitCommandQueuesAndAllocators()
    {
        D3D12_COMMAND_QUEUE_DESC graphicsQueueDesc = {};
        graphicsQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        graphicsQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        graphicsQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        graphicsQueueDesc.NodeMask = 0;
        ThrowIfFailed(m_device->CreateCommandQueue(&graphicsQueueDesc, IID_PPV_ARGS(m_graphicsQueue.ReleaseAndGetAddressOf())));

        D3D12_COMMAND_QUEUE_DESC copyQueueDesc = {};
        copyQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        copyQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        copyQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        copyQueueDesc.NodeMask = 0;
        ThrowIfFailed(m_device->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(m_copyQueue.ReleaseAndGetAddressOf())));

        D3D12_COMMAND_QUEUE_DESC computeQueueDesc = {};
        computeQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        computeQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        computeQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        computeQueueDesc.NodeMask = 0;
        ThrowIfFailed(m_device->CreateCommandQueue(&computeQueueDesc, IID_PPV_ARGS(m_computeQueue.ReleaseAndGetAddressOf())));

        // Create command allocators for each type
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_graphicsCommandAllocator.ReleaseAndGetAddressOf())));
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(m_graphicsCommandAllocator.ReleaseAndGetAddressOf())));
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(m_graphicsCommandAllocator.ReleaseAndGetAddressOf())));
    }

    void Manager::InitDescriptorHeaps()
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
            m_descriptorHeaps[i].Init(GetDevice(), NumDescriptors[type], type, Flags[type]);
        }
    }

    void Manager::InitResourceAllocator()
    {
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED;
        desc.pDevice = m_device.Get();;
        desc.PreferredBlockSize = 0;
        desc.pAllocationCallbacks = nullptr;
        desc.pAdapter = m_dxgiAdapter.Get();
        ThrowIfFailed(D3D12MA::CreateAllocator(&desc, m_D3D12Allocator.ReleaseAndGetAddressOf()));
    }


} // Neb::nri namespace