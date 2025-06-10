#include "NvRtxgiNRC.h"

#include "common/Log.h"
#include "common/Assert.h"
#include "nri/Device.h"
#include "nri/Swapchain.h"

#include "NrcSecurity.h"

#include <algorithm>
#include <ranges>
#include <numeric>

#define CHECK_NRC_STATUS(a, ...) Neb::nri::ThrowIfFalse(a == nrc::Status::OK, ##__VA_ARGS__)

namespace Neb::nri
{

    static void NRCAllocate(size_t bytes)
    {
        NEB_ASSERT(false, "Noimpl");
    }

    // Pointer to function handling logging of SDK messages
    //
    // typedef void (*CustomLoggerPtr)(const char* message, LogLevel logLevel);
    static void NRCLoggerCallback(const char* message, nrc::LogLevel logLevel)
    {
        switch (logLevel)
        {
        case nrc::LogLevel::Debug:
        case nrc::LogLevel::Info: NEB_LOG_INFO("[RTXGI-Nrc] -> {}", message); break;
        case nrc::LogLevel::Warning: NEB_LOG_WARN("[RTXGI-Nrc] -> {}", message); break;
        case nrc::LogLevel::Error:
            NEB_LOG_ERROR("[RTXGI-Nrc] -> {}", message);
            NEB_ASSERT(false);
            break;
        default: NEB_ASSERT(false); break;
        }
    }

    // Pointer to function handling memory allocator messages
    //
    // typedef void (*MemoryEventsLoggerPtr)(MemoryEventType eventType, size_t size, const char* bufferName);
    static void NRCMemoryLoggerCallback(nrc::MemoryEventType eventType, size_t size, const char* bufferName)
    {
        std::string_view name = bufferName ? bufferName : "Unnamed resource";
        switch (eventType)
        {
        case nrc::MemoryEventType::Allocation: NEB_LOG_INFO("[RTXGI-Nrc] -> Memory allocation, resource '{}' ({} bytes)", name, size); break;
        case nrc::MemoryEventType::Deallocation: NEB_LOG_INFO("[RTXGI-Nrc] -> Memory deallocation, resource '{}' ({} bytes)", name, size); break;
        case nrc::MemoryEventType::MemoryStats: NEB_LOG_INFO("[RTXGI-Nrc] -> Memory stats for resource '{}' (current size: {} bytes)", name, size); break;
        default: NEB_ASSERT(false); break;
        }
    }

    static std::wstring GetDllPath(const std::string& dllName)
    {
        HMODULE hMod = GetModuleHandle(dllName.c_str());

        wchar_t path[MAX_PATH];
        DWORD size = GetModuleFileNameW(hMod, path, MAX_PATH);
        assert(size != 0);

        return std::wstring(path, size);
    }

    void NvRtxgiNRCIntegration::Init()
    {
        nri::ThrowIfFalse(nrc::security::VerifySignature(GetDllPath("NRC_D3D12.dll").c_str()), "Failed to verify RTXGI NRC D3D12 DLL!");

        nrc::GlobalSettings globalSettings;
        globalSettings.loggerFn = NRCLoggerCallback;
        globalSettings.memoryLoggerFn = NRCMemoryLoggerCallback;
        globalSettings.allocatorFn = nullptr;
        globalSettings.deallocatorFn = nullptr;

        globalSettings.enableGPUMemoryAllocation = true; // Firstly do it for us! TODO: add manual!
        globalSettings.enableDebugBuffers = true;

        globalSettings.maxNumFramesInFlight = Swapchain::NumBackbuffers;

        // Initialize the NRC Library
        {
            NEB_LOG_INFO("Initializing NRC D3D12 global settings");
            CHECK_NRC_STATUS(nrc::d3d12::Initialize(globalSettings), "Failed to initialize NRC for D3D12 with provided global settings");
        }

        // Create NRC device context
        {
            NEB_LOG_INFO("Initializing NRC D3D12 device context");
            NRIDevice& device = NRIDevice::Get();
            CHECK_NRC_STATUS(nrc::d3d12::Context::Create(device.GetD3D12Device(), m_nrcContext), "Failed to initialize NRC D3D12 context for device");
        }
    }

    void NvRtxgiNRCIntegration::Destroy()
    {
        if (m_nrcContext)
        {
            nrc::d3d12::Context::Destroy(*m_nrcContext);
            m_nrcContext = nullptr;
        }

        nrc::d3d12::Shutdown();
    }

    void NvRtxgiNRCIntegration::Configure(const nrc::ContextSettings& settings)
    {
        m_contextSettings = settings; // update configuration

        nrc::BuffersAllocationInfo allocationInfo;
        nri::ThrowIfFalse(nrc::d3d12::Context::GetBuffersAllocationInfo(settings, allocationInfo) == nrc::Status::OK,
            "Failed to obtain buffer allocation info");

        // NRC library manages memory in this case.
        // Pass it current configuration
        nrc::Status status = m_nrcContext->Configure(settings);
        nri::ThrowIfFalse(status == nrc::Status::OK, "Failed to configure internal NRC buffers");

        // The NRC SDK is managing buffer allocations, so we need to pull those native buffers into NVRHI
        const nrc::d3d12::Buffers* buffers = m_nrcContext->GetBuffers();
        for (uint32_t i = 0; i < EnumValue(nrc::BufferIdx::Count); ++i)
        {
            const nrc::d3d12::BufferInfo& bufferInfo = buffers->buffers[i];
            const nrc::BufferIdx bufferIdx = static_cast<nrc::BufferIdx>(i);
            m_nrcBuffers[i] = NRCBuffer{
                .resource = bufferInfo.resource,
                .info = allocationInfo.allocationInfo[i],
                .allocationSizeInBytes = bufferInfo.allocatedSize
            };
        }
    }

    void NvRtxgiNRCIntegration::BeginFrame(Rc<ID3D12GraphicsCommandList4> commandList, const nrc::FrameSettings& frameSettings)
    {
        CHECK_NRC_STATUS(m_nrcContext->BeginFrame(commandList.Get(), frameSettings), "Failed to begin frame with NRC context");
    }

    void NvRtxgiNRCIntegration::EndFrame(ID3D12CommandQueue* commandQueue)
    {
        CHECK_NRC_STATUS(m_nrcContext->EndFrame(commandQueue), "Failed to end frame with NRC context");
    }

    void NvRtxgiNRCIntegration::QueryAndTrain(Rc<ID3D12GraphicsCommandList4> commandList, float* pTrainingLoss)
    {
        CHECK_NRC_STATUS(m_nrcContext->QueryAndTrain(commandList.Get(), nullptr), "Failed to query and train NRC (NRC QueryAndTrain call failed)");
    }

    void NvRtxgiNRCIntegration::Resolve(Rc<ID3D12GraphicsCommandList4> commandList, ID3D12Resource* outputResource)
    {
        CHECK_NRC_STATUS(m_nrcContext->Resolve(commandList.Get(), outputResource), "Failed to resolve predicted radiance into outputResource");
    }

    void NvRtxgiNRCIntegration::PopulateShaderConstants(NrcConstants* pOutConstants) const
    {
        CHECK_NRC_STATUS(m_nrcContext->PopulateShaderConstants(*pOutConstants), "Failed to populate NRC shader constants");
    }

    size_t NvRtxgiNRCIntegration::GetMemoryFootprint() const
    {
        size_t totalBytes = std::ranges::fold_left(m_nrcBuffers | std::views::transform(&NRCBuffer::allocationSizeInBytes), 0, std::plus());
        return totalBytes;
    }

} // Neb::nri namespace