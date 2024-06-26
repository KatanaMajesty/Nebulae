#include "Raytracer.h"

#include "common/Defines.h"
#include "common/Log.h"
#include "nri/Manager.h"

namespace Neb
{

    bool Raytracer::Init(const RaytracingContext& context)
    {
        NEB_ASSERT(context.Swapchain && context.DepthStencilBuffer);
        m_context = context;

        nri::Manager& device = nri::Manager::Get();
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
        nri::Manager& device = nri::Manager::Get();

        //device.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, )
    }

    void Raytracer::InitRootSignatureAndShaders()
    {
    }

}