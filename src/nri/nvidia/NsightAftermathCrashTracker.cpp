#include "NsightAftermathCrashTracker.h"

#include "common/Assert.h"

#include "NsightAftermathHelpers.h"

#include <cstring>
#include <cctype> // std::tolower
#include <filesystem>
#include <format>
#include <fstream>
#include <algorithm>

#define AFTERMATH_NOIMPL(...) NEB_ASSERT(false, ##__VA_ARGS__)

namespace Neb::nri
{

    NvNsightAftermathCrashTracker::~NvNsightAftermathCrashTracker()
    {
        // We guarantee that NvNsightAftermathCrashTracker::Destroy will reset m_isInitialized to false
        // we assert against that here so that we are sure that Aftermath is not initialized on application shutdown
        //
    }


    void NvNsightAftermathCrashTracker::Init(ENvNsightAftermath_CrashTrackerFlags flags)
    {
        NEB_ASSERT(!IsInitialized(), "Tried initializing more than once!");
        if (IsInitialized())
            return;

        AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_EnableGpuCrashDumps(
            GFSDK_Aftermath_Version_API,
            GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
            GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, // Let the Nsight Aftermath library cache shader debug information.
            OnCrashDump,
            OnShaderDebugInfo,
            OnCrashDumpDescription,
            OnResolveMarker, nullptr));

        m_flags = flags;
        m_isInitialized = true;
    }

    void NvNsightAftermathCrashTracker::Destroy()
    {
        m_isInitialized = false;
    }

    void NvNsightAftermathCrashTracker::OnCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData)
    {
        NEB_LOG_INFO("NvNsightAftermathCrashTracker::OnCrashDump");
        NvNsightAftermathCrashTracker::Get()->WriteCrashDumpToFile(pGpuCrashDump, gpuCrashDumpSize);
    }

    void NvNsightAftermathCrashTracker::OnShaderDebugInfo(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData)
    {
        // Get shader debug information identifier
        GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier = {};
        AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GetShaderDebugInfoIdentifier(
            GFSDK_Aftermath_Version_API,
            pShaderDebugInfo,
            shaderDebugInfoSize,
            &identifier));

        // Write to file for later in-depth analysis of crash dumps with Nsight Graphics
        // NEB_LOG_INFO("NvNsightAftermathCrashTracker::OnShaderDebugInfo");
        NvNsightAftermathCrashTracker::Get()->WriteShaderDebugInformationToFile(identifier, pShaderDebugInfo, shaderDebugInfoSize);
    }

    void NvNsightAftermathCrashTracker::OnCrashDumpDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addValue, void* pUserData)
    {
        NEB_ASSERT(NvNsightAftermathCrashTracker::Get()->IsInitialized(), "NvNsightAftermathCrashTracker::OnCrashDumpDescription -> Aftermath was not initialized correctly");
        NEB_LOG_INFO("NvNsightAftermathCrashTracker::OnCrashDumpDescription");

        // Add some basic description about the crash. This is called after the GPU crash happens, but before
        // the actual GPU crash dump callback. The provided data is included in the crash dump and can be
        // retrieved using GFSDK_Aftermath_GpuCrashDump_GetDescription().
        addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "Nebulae Application");
        addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "v1.0");
        addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined, "This is Nsight Aftermath crash dump from Nebulae");
        // addValue(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined + 1, "Engine state: x");
    }

    void NvNsightAftermathCrashTracker::OnResolveMarker(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize)
    {
        NEB_LOG_INFO("NvNsightAftermathCrashTracker::OnResolveMarker");
        std::span<uint8_t> resolvedData = NvNsightAftermathCrashTracker::Get()->ResolveMarker(pMarkerData, markerDataSize);
        *ppResolvedMarkerData = resolvedData.data();
        *pResolvedMarkerDataSize = resolvedData.size_bytes();
    }

    void NvNsightAftermathCrashTracker::WriteCrashDumpToFile(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize)
    {
        NEB_ASSERT(IsInitialized(), "NvNsightAftermathCrashTracker::WriteCrashDumpToFile -> Aftermath was not initialized correctly");

        std::lock_guard lock(m_trackerMutex);
        {
            // Create a GPU crash dump decoder object for the GPU crash dump.
            GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
            AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_CreateDecoder(GFSDK_Aftermath_Version_API,
                pGpuCrashDump, gpuCrashDumpSize, &decoder));

            // Use the decoder object to read basic information, like application
            // name, PID, etc. from the GPU crash dump.
            GFSDK_Aftermath_GpuCrashDump_BaseInfo baseInfo = {};
            AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_GetBaseInfo(decoder, &baseInfo));

            // Sanity checks
            NEB_ASSERT(baseInfo.graphicsApi == GFSDK_Aftermath_GraphicsApi_D3D_12_0, "For whatever reason graphics APIs differ");

            // Use the decoder object to query the application name that was set
            // in the GPU crash dump description.
            uint32_t applicationNameLength = 0;
            AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize(decoder,
                GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
                &applicationNameLength));

            std::string applicationName;
            applicationName.resize(applicationNameLength);
            {
                AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_GetDescription(decoder,
                    GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
                    static_cast<uint32_t>(applicationName.size()), applicationName.data()));
            }
            applicationName.pop_back(); // pop-back last character (it is an extra null-termination)

            static constexpr auto toLower = [](unsigned char c) { return std::tolower(c); };
            std::transform(applicationName.begin(), applicationName.end(), applicationName.begin(), toLower);
            std::ranges::replace(applicationName, ' ', '_');

            // Create a unique file name for writing the crash dump data to a file.
            // Note: due to an Nsight Aftermath bug (will be fixed in an upcoming
            // driver release) we may see redundant crash dumps. As a workaround,
            // attach a unique count to each generated file name.
            static uint32_t count = 0;
            std::filesystem::path filename = std::format("{}-{}_{}", applicationName, baseInfo.pid, ++count);
            std::filesystem::path filepath = std::filesystem::current_path() / filename;

            std::ofstream dumpFile(filepath.replace_extension(NvNsightAftermathCrashTracker::CrashdumpExtesnion), std::ios::out | std::ios::binary);
            if (dumpFile)
            {
                dumpFile.write((const char*)pGpuCrashDump, gpuCrashDumpSize);
                dumpFile.close();
            }

            // TODO: Add flag to generate json
            if (m_flags & eNvNsightAftermath_CrashTrackerFlag_GenerateJsonDump)
            {
                AFTERMATH_NOIMPL("No implementation for Json dumps yet! Revisit!");

                // Decode the crash dump to a JSON string.
                // Step 1: Generate the JSON and get the size.
                uint32_t jsonSize = 0;
                AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_GenerateJSON(decoder,
                    GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO, GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
                    OnShaderDebugInfoLookup, OnShaderLookup, OnShaderSourceDebugInfoLookup,
                    nullptr,
                    &jsonSize));

                // Step 2: Allocate a buffer and fetch the generated JSON.
                std::string json;
                json.resize(jsonSize);
                AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_GetJSON(decoder,
                    static_cast<uint32_t>(json.size()), json.data()));
                // TODO: Check if the name is correct!

                // Write the crash dump data as JSON to a file.
                std::ofstream jsonFile(filepath.replace_extension(".json"), std::ios::out | std::ios::binary);
                if (jsonFile)
                {
                    // Write the JSON to the file (excluding string termination)
                    jsonFile.write(json.data(), json.size() - 1);
                    jsonFile.close();
                }
            }

            // Destroy the GPU crash dump decoder object.
            AFTERMATH_THROW_IF_FAILED(GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder));
        }
    }

    void NvNsightAftermathCrashTracker::WriteShaderDebugInformationToFile(
        GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
        const void* pShaderDebugInfo,
        const uint32_t shaderDebugInfoSize)
    {
        NEB_ASSERT(IsInitialized(), "NvNsightAftermathCrashTracker::WriteShaderDebugInformationToFile -> Aftermath was not initialized correctly");

        std::lock_guard lock(m_trackerMutex);
        {
            // Store information for decoding of GPU crash dumps with shader address mapping
            // from within the application.
            m_shaderDebugInfo[identifier] = std::vector<uint8_t>(shaderDebugInfoSize);
            std::memcpy(m_shaderDebugInfo[identifier].data(), pShaderDebugInfo, shaderDebugInfoSize);
        }
    }

    std::span<uint8_t> NvNsightAftermathCrashTracker::ResolveMarker(const void* pMarkerData, const uint32_t markerDataSize)
    {
        NEB_ASSERT(IsInitialized(), "NvNsightAftermathCrashTracker::ResolveMarker -> Aftermath was not initialized correctly");
        NEB_LOG_INFO("NvNsightAftermathCrashTracker::ResolveMarker -> No implementation yet, we are not using markers");
        return std::span<uint8_t>();
    }

    void NvNsightAftermathCrashTracker::OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* pIdentifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo, void* pUserData)
    {
    }

    void NvNsightAftermathCrashTracker::OnShaderLookup(const GFSDK_Aftermath_ShaderBinaryHash* pShaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* pUserData)
    {
    }

    void NvNsightAftermathCrashTracker::OnShaderSourceDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugName* pShaderDebugName, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* pUserData)
    {
    }

    std::span<uint8_t> NvNsightAftermathShaderDatabase::GetShaderBinary(GFSDK_Aftermath_ShaderBinaryHash hash)
    {
        AFTERMATH_NOIMPL("NvNsightAftermathShaderDatabase is not implemented yet!");
        return std::span<uint8_t>();
    }

    std::span<const uint8_t> NvNsightAftermathShaderDatabase::GetShaderBinary(GFSDK_Aftermath_ShaderBinaryHash hash) const
    {
        AFTERMATH_NOIMPL("NvNsightAftermathShaderDatabase is not implemented yet!");
        return std::span<const uint8_t>();
    }

    std::span<uint8_t> NvNsightAftermathShaderDatabase::GetShaderDebugData(const GFSDK_Aftermath_ShaderDebugName& debugName)
    {
        AFTERMATH_NOIMPL("NvNsightAftermathShaderDatabase is not implemented yet!");
        return std::span<uint8_t>();
    }

    std::span<const uint8_t> NvNsightAftermathShaderDatabase::GetShaderDebugData(const GFSDK_Aftermath_ShaderDebugName& debugName) const
    {
        AFTERMATH_NOIMPL("NvNsightAftermathShaderDatabase is not implemented yet!");
        return std::span<const uint8_t>();
    }

} // Neb::nri namespace