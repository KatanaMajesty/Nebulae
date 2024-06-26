#pragma once

#include "core/Scene.h"
#include "nri/Swapchain.h"
#include "nri/DepthStencilBuffer.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"

namespace Neb
{

    // All the external resources that will be passed from renderer
    struct RaytracingContext
    {
        nri::Swapchain* Swapchain = nullptr;
        nri::DepthStencilBuffer* DepthStencilBuffer = nullptr;
    };

    class Raytracer
    {
    public:
        bool Init(const RaytracingContext& context);
        void RenderScene(Scene* scene);

    private:
        RaytracingContext m_context;

        void InitCommandList();
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_commandList;

        void InitRootSignatureAndShaders();
        nri::D3D12Rc<ID3D12RootSignature> m_rootSignature;
        nri::ShaderCompiler m_shaderCompiler;
        nri::Shader m_vs;
        nri::Shader m_ps;
    };

} // Neb namespace