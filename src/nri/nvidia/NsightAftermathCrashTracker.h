#pragma once

#include "Win.h"

#include "NVIDIA/Nsight_Aftermath/include/GFSDK_Aftermath.h"
#include "NVIDIA/Nsight_Aftermath/include/GFSDK_Aftermath_GpuCrashDump.h"
#include "NVIDIA/Nsight_Aftermath/include/GFSDK_Aftermath_GpuCrashDumpDecoding.h"

#include <span>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>
#include <map>

namespace Neb::nri
{

    struct NvNsightAftermathMarkerMap
    {
        using MapType = std::map<uint64_t, std::string>;

        static constexpr uint32_t NumHistoryFrames = 4;

        std::array<MapType, NumHistoryFrames> m_markerMapHistory;
    }; // NvNsightAftermathMarkerMap struct

    // TODO: Not used yet, add impl
    class NvNsightAftermathShaderDatabase
    {
    public:
        NvNsightAftermathShaderDatabase() = default;

        // Returns a shader binary for the corresponding hash or an empty std::span with data() == nullptr (defaultly constructed)
        std::span<uint8_t> GetShaderBinary(GFSDK_Aftermath_ShaderBinaryHash hash);
        std::span<const uint8_t> GetShaderBinary(GFSDK_Aftermath_ShaderBinaryHash hash) const;

        std::span<uint8_t> GetShaderDebugData(const GFSDK_Aftermath_ShaderDebugName& debugName);
        std::span<const uint8_t> GetShaderDebugData(const GFSDK_Aftermath_ShaderDebugName& debugName) const;
    }; // NvNsightAftermathShaderDatabase class

    using ENvNsightAftermath_CrashTrackerFlags = uint8_t;
    enum ENvNsightAftermath_CrashTrackerFlag
    {
        eNvNsightAftermath_CrashTrackerFlag_None = 0,
        eNvNsightAftermath_CrashTrackerFlag_GenerateJsonDump = 1,
    }; // ENvNsightAftermath_CrashTrackerFlag enum

    // Crash tracker may be safely used on multiple threads
    class NvNsightAftermathCrashTracker
    {
    private:
        NvNsightAftermathCrashTracker() = default;
        ~NvNsightAftermathCrashTracker(); // Destructor is used to check whether or not Aftermath was destroyed correctly

    public:
        static constexpr std::string_view CrashdumpExtesnion = "nv-gpudmp";

        NvNsightAftermathCrashTracker(const NvNsightAftermathCrashTracker&) = delete;
        NvNsightAftermathCrashTracker& operator=(const NvNsightAftermathCrashTracker&) = delete;

        static NvNsightAftermathCrashTracker* Get()
        {
            static NvNsightAftermathCrashTracker instance;
            return &instance;
        }

        void Init(ENvNsightAftermath_CrashTrackerFlags flags = eNvNsightAftermath_CrashTrackerFlag_None);
        void Destroy();
        bool IsInitialized() const { return m_isInitialized; }

    private:
        // GFSDK_Aftermath callbacks
        static void OnCrashDump(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData);
        static void OnShaderDebugInfo(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData);
        static void OnCrashDumpDescription(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addValue, void* pUserData);
        static void OnResolveMarker(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize);

        void WriteCrashDumpToFile(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize);
        void WriteShaderDebugInformationToFile(
            GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier,
            const void* pShaderDebugInfo,
            const uint32_t shaderDebugInfoSize);

        // Returns persistently stored resolved marker data
        std::span<uint8_t> ResolveMarker(const void* pMarkerData, const uint32_t markerDataSize);

        // GFSDK_Aftermath Shader lookup callbacks (for GFSDK_Aftermath_GpuCrashDump_GenerateJSON)
        static void OnShaderDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugInfoIdentifier* pIdentifier, PFN_GFSDK_Aftermath_SetData setShaderDebugInfo, void* pUserData);
        static void OnShaderLookup(const GFSDK_Aftermath_ShaderBinaryHash* pShaderHash, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* pUserData);
        static void OnShaderSourceDebugInfoLookup(const GFSDK_Aftermath_ShaderDebugName* pShaderDebugName, PFN_GFSDK_Aftermath_SetData setShaderBinary, void* pUserData);

        std::atomic_bool m_isInitialized = false;
        ENvNsightAftermath_CrashTrackerFlags m_flags = eNvNsightAftermath_CrashTrackerFlag_None;

        mutable std::mutex m_trackerMutex;
        std::map<GFSDK_Aftermath_ShaderDebugInfoIdentifier, std::vector<uint8_t>> m_shaderDebugInfo;

        NvNsightAftermathMarkerMap m_markerMap;
    };

} // Neb::nri namespace