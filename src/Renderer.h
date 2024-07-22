#pragma once

#include "core/Scene.h"

#include "nri/ConstantBuffer.h"
#include "nri/DepthStencilBuffer.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

#include "Raytracer.h"

#include <array>

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
        static constexpr UINT NumInflightFrames = nri::Swapchain::NumBackbuffers;

        BOOL Init(HWND hwnd);

        void RenderScene(float timestep, Scene* scene);
        void Resize(UINT width, UINT height);

        UINT GetWidth() const { return m_swapchain.GetWidth(); }
        UINT GetHeight() const { return m_swapchain.GetHeight(); }

    private:
        void PopulateCommandLists(UINT frameIndex, float timestep, Scene* scene);

        // Synchronizes with in-flight, waits if needed
        // returns  frame index of the next frame (as a swapchain's backbuffer index)
        UINT NextFrame();
        void WaitForAllFrames();

        nri::ShaderCompiler m_shaderCompiler;
        nri::Swapchain m_swapchain;
        nri::DepthStencilBuffer m_depthStencilBuffer;
        nri::D3D12Rc<ID3D12Fence> m_fence;
        UINT m_frameIndex = 0;
        UINT64 m_fenceValues[NumInflightFrames];

        void InitCommandList();
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_commandList;

        void InitRootSignatureAndShaders();
        nri::D3D12Rc<ID3D12RootSignature> m_rootSignature;
        nri::Shader m_vsBasic;
        nri::Shader m_psBasic;

        void InitPipelineState();
        nri::D3D12Rc<ID3D12PipelineState> m_pipelineState;

        void InitInstanceCb();
        nri::ConstantBuffer m_cbInstance;
    };

}