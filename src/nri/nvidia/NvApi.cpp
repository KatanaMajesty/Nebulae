#include "NvApi.h"

#include "common/Assert.h"
#include "common/Log.h"

#include <NVIDIA/NvApi/nvapi.h>

namespace Neb::nri
{

    static void DXRMessageCallback(void* pUserData,
        NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity,
        const char* messageCode,
        const char* message,
        const char* messageDetails)
    {
        switch (severity)
        {
        case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR:
            NEB_LOG_ERROR("DXR Error ({}): {}\n\t{}", messageCode, message, messageDetails);
            break;
        case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING:
            NEB_LOG_WARN("DXR Warning ({}): {}\n\t{}", messageCode, message, messageDetails);
            break;
        default:
            NEB_LOG_INFO("DXR ({}): {}\n\t{}", messageCode, message, messageDetails);
        }
    }

    static std::string NVGetStatusString(NvAPI_Status status)
    {
        NvAPI_ShortString str;
        NEB_ASSERT(NvAPI_GetErrorMessage(status, str) == NVAPI_OK);
        return std::string(str);
    }

    bool NvDriver::InitD3D12(Rc<ID3D12Device5> device)
    {
        if (m_device)
        {
            NEB_LOG_WARN("NvDriver -> Previously assigned D3D12 device will be de-initialized!");
            ShutD3D12();
            m_device = nullptr;
        }

        m_device = device;
        ID3D12Device5* pDevice = device.Get();

        NvAPI_Status status = NvAPI_Initialize();
        if (status != NVAPI_OK)
        {
            NEB_LOG_ERROR("Failed to initialize NvDriver -> {}", NVGetStatusString(status));
            return false;
        }

        status = NvAPI_D3D12_EnableRaytracingValidation(pDevice, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE);
        if (status != NVAPI_OK)
        {
            NEB_LOG_ERROR("Failed to enable DXR validation -> {}", NVGetStatusString(status));
            // print some hints
            switch (status)
            {
            case NVAPI_ACCESS_DENIED: NEB_LOG_INFO("\tTo resolve access denied issue set NV_ALLOW_RAYTRACING_VALIDATION=1 environment variable!"); break;
            }
            return false;
        }

        status = NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(pDevice, &DXRMessageCallback, nullptr, &m_dxrMessengerHandle);
        if (status != NVAPI_OK)
        {
            NEB_LOG_ERROR("Failed to register DXR messenger -> {}", NVGetStatusString(status));
            return false;
        }

        return true;
    }

    void NvDriver::ShutD3D12()
    {
        NEB_ASSERT(IsValid(), "Device context is not valid for NvAPI driver");

        ID3D12Device5* pDevice = m_device.Get();
        NvAPI_Status status;

        if (m_dxrMessengerHandle)
        {
            status = NvAPI_D3D12_UnregisterRaytracingValidationMessageCallback(pDevice, m_dxrMessengerHandle);
            NEB_ASSERT(status == NVAPI_OK, "Failed to unregister DXR messenger -> {}", NVGetStatusString(status));
            m_dxrMessengerHandle = nullptr;
        }
    }

} // Neb::nri namespace
