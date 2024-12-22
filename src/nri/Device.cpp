#include "Device.h"

// Chill here for getting started -> https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component

namespace Neb::nri
{
    NRIDevice& NRIDevice::Get()
    {
        static NRIDevice instance;
        return instance;
    }

    void NRIDevice::Init()
    {
        InitPipeline();
    }

    void NRIDevice::Deinit()
    {
        if (m_debugDevice)
        {
            D3D12_RLDO_FLAGS rldoFlags = D3D12_RLDO_DETAIL;
            m_debugDevice->ReportLiveDeviceObjects(rldoFlags);
        }

        if (m_debugInfoQueue)
        {
            NEB_LOG_INFO("Unregistering validation layer message callback");
            ThrowIfFailed(m_debugInfoQueue->UnregisterMessageCallback(m_debugCallbackID));
        }
    }

    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-a-basic-direct3d-12-component#loadpipeline
    void NRIDevice::InitPipeline()
    {
        // Enable the debug layer
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(m_debugInterface.ReleaseAndGetAddressOf())));
        m_debugInterface->EnableDebugLayer();
#if defined(NEB_DEBUG)
        m_debugInterface->SetEnableGPUBasedValidation(TRUE);
#endif // defined(NEB_DEBUG)

        // Create the factory
        Rc<IDXGIFactory2> factory;
        ThrowIfFailed(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(factory.GetAddressOf())));
        ThrowIfFailed(factory->QueryInterface(IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

        // Query appropriate dxgi adapter
        ThrowIfFalse(QueryMostSuitableDeviceAdapter());

        Rc<ID3D12Device> device;
        ThrowIfFailed(D3D12CreateDevice(m_dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf())));
        ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf())));

        this->InitDebugDeviceContext();

        // Query device capabilities and record them into manager's capabilities struct
        m_capabilities = {};
        m_capabilities.IsScreenTearingSupported = QueryDxgiFactoryTearingSupport();
        m_capabilities.MeshShaderSupportTier = QueryDeviceMeshShaderSupportTier();
        m_capabilities.RaytracingSupportTier = QueryDeviceRaytracingSupportTier();

        // Do not care if no mesh shader or raytracing support available
        ThrowIfFalse(m_capabilities.MeshShaderSupportTier != ESupportTier_MeshShader::NotSupported);
        ThrowIfFalse(m_capabilities.RaytracingSupportTier != ESupportTier_Raytracing::NotSupported);

        // Fill out a command queue description, then create command queues, allocators and fences
        InitCommandContexts();

        // Fill out a heap description. then create a descriptor heap
        InitDescriptorHeaps();

        InitResourceAllocator();
    }

    BOOL NRIDevice::IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const
    {
        // Don't select render driver, provided by D3D12. We only use physical hardware
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            return FALSE;
        }

        // Passing nullptr as a parameter would just test the adapter for compatibility
        Rc<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(DxgiAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()))))
        {
            return FALSE;
        }

        return TRUE;
    }

    BOOL NRIDevice::QueryMostSuitableDeviceAdapter()
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

    namespace validation
    {
        static std::string_view GetMessageCategoryAsStr(D3D12_MESSAGE_CATEGORY category)
        {
            switch (category)
            {
            case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APP_DEFINED";
            case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISC";
            case D3D12_MESSAGE_CATEGORY_INITIALIZATION: return "INIT";
            case D3D12_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
            case D3D12_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
            case D3D12_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
            case D3D12_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
            case D3D12_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
            case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIP";
            case D3D12_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
            case D3D12_MESSAGE_CATEGORY_SHADER:
                return "SHADER";
            default:
                NEB_ASSERT(false);
                return "UNKNOWN";
            }
        }

        static std::string_view GetMessageSeverityAsStr(D3D12_MESSAGE_SEVERITY severity)
        {
            switch (severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
            case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
            case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
            case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
            case D3D12_MESSAGE_SEVERITY_MESSAGE:
                return "MESSAGE";
            default:
                NEB_ASSERT(false);
                return "UNKNOWN";
            }
        }

        void OnDebugLayerMessage(D3D12_MESSAGE_CATEGORY category,
            D3D12_MESSAGE_SEVERITY severity,
            D3D12_MESSAGE_ID ID,
            LPCSTR description,
            void* vpDevice)
        {
            std::string_view pCategory = GetMessageCategoryAsStr(category);
            std::string_view pSeverity = GetMessageSeverityAsStr(severity);
            std::string message = std::format("(Severity: {}) [Category: {}], ID {}: {}",
                pSeverity,
                pCategory,
                static_cast<UINT>(ID),
                description);

            switch (severity)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
            case D3D12_MESSAGE_SEVERITY_ERROR:
            {
                NEB_LOG_ERROR(message);
            };
            break;
            case D3D12_MESSAGE_SEVERITY_WARNING:
            {
                NEB_LOG_WARN(message);
            };
            break;
            case D3D12_MESSAGE_SEVERITY_INFO:
            case D3D12_MESSAGE_SEVERITY_MESSAGE:
            {
                NEB_LOG_INFO(message);
            };
            break;
            default:
                NEB_ASSERT(false);
                break;
            }
        }
    }; // validation namespace

    void NRIDevice::InitDebugDeviceContext()
    {
#if defined(NEB_DEBUG)
        ThrowIfFailed(m_device->QueryInterface(IID_PPV_ARGS(m_debugDevice.ReleaseAndGetAddressOf())),
            "Could not retrieve debug device, was D3D12GetDebugInterface call successful?");

        HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(m_debugInfoQueue.ReleaseAndGetAddressOf()));
        NEB_LOG_WARN_IF(FAILED(hr), "Failed to obtain device debug information queue. Validation layer messaging callback won't be initialized correctly!");

        if (SUCCEEDED(hr))
        {
            D3D12_MESSAGE_ID deniedIDs[] = {
                // Suppress D3D12 ERROR: ID3D12CommandQueue::Present: Resource state (0x800: D3D12_RESOURCE_STATE_COPY_SOURCE)
                // As it thats a bug in the DXGI Debug Layer interaction with the DX12 Debug Layer w/ Windows 11.
                // The issue comes from debug layer interacting with hybrid graphics, such as integrated and discrete laptop GPUs
                D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
            };

            D3D12_INFO_QUEUE_FILTER_DESC denyFilterDesc{
                .NumIDs = _countof(deniedIDs),
                .pIDList = deniedIDs
            };

            D3D12_INFO_QUEUE_FILTER filter{
                .AllowList = {},
                .DenyList = { denyFilterDesc }
            };

            static_assert(sizeof(DWORD) == sizeof(UINT) && "DWORD should still push 4 bytes on a 64-bit processor");
            ThrowIfFailed(m_debugInfoQueue->AddStorageFilterEntries(&filter));
            ThrowIfFailed(m_debugInfoQueue->RegisterMessageCallback(validation::OnDebugLayerMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &(DWORD&)m_debugCallbackID));
        }
#endif // defined(NEBD_DEBUG)
    }

    BOOL NRIDevice::QueryDxgiFactoryTearingSupport() const
    {
        BOOL allowTearing = FALSE;
        ThrowIfFailed(m_dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)));
        return allowTearing;
    }

    ESupportTier_MeshShader NRIDevice::QueryDeviceMeshShaderSupportTier() const
    {
        // https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html#checkfeaturesupport
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 featureSupportData = {};
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &featureSupportData, sizeof(featureSupportData))) ||
            featureSupportData.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED)
        {
            return ESupportTier_MeshShader::NotSupported;
        }

        // TODO: As of now there is only 1 support tier of mesh shaders
        return ESupportTier_MeshShader::SupportTier_1_0;
    }

    ESupportTier_Raytracing NRIDevice::QueryDeviceRaytracingSupportTier() const
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData))) ||
            featureSupportData.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
        {
            return ESupportTier_Raytracing::NotSupported;
        }

        switch (featureSupportData.RaytracingTier)
        {
        case D3D12_RAYTRACING_TIER_1_0: return ESupportTier_Raytracing::SupportTier_1_0;
        case D3D12_RAYTRACING_TIER_1_1: return ESupportTier_Raytracing::SupportTier_1_1;
        default: return ESupportTier_Raytracing::NotSupported; // as of now only 2 support tiers are supported;
        }
    }

    void NRIDevice::InitCommandContexts()
    {
        static constexpr std::array D3D12TypeMap = {
            D3D12_COMMAND_LIST_TYPE_DIRECT,  // eCommandContextType_Graphics
            D3D12_COMMAND_LIST_TYPE_COPY,    // eCommandContextType_Copy
            D3D12_COMMAND_LIST_TYPE_COMPUTE, // eCommandContextType_Compute
        };

        for (size_t i = 0; i < D3D12TypeMap.size(); ++i)
        {
            const D3D12_COMMAND_LIST_TYPE type = D3D12TypeMap[i];

            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = type;
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0;
            ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_commandQueues[i].ReleaseAndGetAddressOf())));

            m_commandAllocatorPools[i] = CommandAllocatorPool(m_device.Get(), type);
        };
    }

    void NRIDevice::InitDescriptorHeaps()
    {
        static constexpr UINT NumDescriptors[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {
            4096, // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            256,  // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            2048, // D3D12_DESCRIPTOR_HEAP_TYPE_RTV
            256,  // D3D12_DESCRIPTOR_HEAP_TYPE_DSV
        };

        static constexpr D3D12_DESCRIPTOR_HEAP_FLAGS Flags[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, // D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, // D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,           // D3D12_DESCRIPTOR_HEAP_TYPE_RTV
            D3D12_DESCRIPTOR_HEAP_FLAG_NONE,           // D3D12_DESCRIPTOR_HEAP_TYPE_DSV
        };

        for (UINT i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            D3D12_DESCRIPTOR_HEAP_TYPE type = (D3D12_DESCRIPTOR_HEAP_TYPE)i;
            m_descriptorHeaps[i].Init(GetDevice(), D3D12_DESCRIPTOR_HEAP_DESC{
                                                       .Type = type,
                                                       .NumDescriptors = NumDescriptors[type],
                                                       .Flags = Flags[type],
                                                   });
        }
    }

    void NRIDevice::InitResourceAllocator()
    {
        D3D12MA::ALLOCATOR_DESC desc = {};
        desc.Flags = D3D12MA::ALLOCATOR_FLAG_MSAA_TEXTURES_ALWAYS_COMMITTED;
        desc.pDevice = m_device.Get();
        ;
        desc.PreferredBlockSize = 0;
        desc.pAllocationCallbacks = nullptr;
        desc.pAdapter = m_dxgiAdapter.Get();
        ThrowIfFailed(D3D12MA::CreateAllocator(&desc, m_D3D12Allocator.ReleaseAndGetAddressOf()));
    }


} // Neb::nri namespace