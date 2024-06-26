#pragma once

#include "core/Scene.h"

#include "nri/DepthStencilBuffer.h"
#include "nri/Manager.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

#include "Raytracer.h"

namespace Neb
{

    // All CBs require 256 alignment
    struct alignas(256) CbInstanceInfo
    {
        Neb::Mat4 InstanceToWorld;
        Neb::Mat4 ViewProj;
    };

    class Renderer
    {
    public:
        BOOL Init(HWND hwnd);
        
        void RenderScene(float timestep, Scene* scene);
        void Resize(UINT width, UINT height);

        UINT GetWidth() const { return m_swapchain.GetWidth(); }
        UINT GetHeight() const { return m_swapchain.GetHeight(); }

    private:
        void WaitForFrameToFinish();

        nri::ShaderCompiler m_shaderCompiler;
        nri::Swapchain m_swapchain;
        nri::DepthStencilBuffer m_depthStencilBuffer;

        void InitCommandList();
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_commandList;
    
        void InitRootSignatureAndShaders();
        nri::D3D12Rc<ID3D12RootSignature> m_rootSignature;
        nri::Shader m_vsBasic;
        nri::Shader m_psBasic;

        void InitPipelineState();
        nri::D3D12Rc<ID3D12PipelineState> m_pipelineState;

        void InitInstanceInfoCb();
        CbInstanceInfo* m_cbInstanceInfoBufferMapping = nullptr;
        nri::D3D12Rc<ID3D12Resource> m_cbInstanceInfoBuffer;
        nri::DescriptorAllocation m_cbInstanceInfoDescriptor;
    };

}