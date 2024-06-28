#include "Raytracer.h"

#include "common/Defines.h"
#include "common/Log.h"
#include "nri/Device.h"

namespace Neb
{

    bool Raytracer::Init(const RaytracingContext& context)
    {
        NEB_ASSERT(context.Swapchain && context.DepthStencilBuffer);
        m_context = context;

        nri::NRIDevice& device = nri::NRIDevice::Get();
        if (device.GetCapabilities().RaytracingSupportTier == nri::ESupportTier_Raytracing::NotSupported)
        {
            NEB_LOG_ERROR("Ray tracing is not supported on this device!");
            return false;
        }

        return true;
    }

    void Raytracer::RenderScene(Scene* scene)
    {
        //m_commandList->Reset();
    }

    void Raytracer::InitCommandList()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        //device.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, )
    }

    void Raytracer::InitRootSignatureAndShaders()
    {
    }

}