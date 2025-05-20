#pragma once

#include "core/Scene.h"

#include "nri/ConstantBuffer.h"
#include "nri/DepthStencilBuffer.h"
#include "nri/RootSignature.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

#include "Raytracer.h"

#include <array>

namespace Neb
{

    CONSTANT_BUFFER_STRUCT CbInstanceInfo
    {
        Neb::Mat4 InstanceToWorld;
        Neb::Mat4 ViewProj;
    };

    class Renderer
    {
    public:
        static constexpr UINT NumInflightFrames = nri::Swapchain::NumBackbuffers;

        Renderer() = default;
        
        Renderer(const Renderer&) = default;
        Renderer& operator=(const Renderer&) = default;

        ~Renderer();

        BOOL Init(HWND hwnd);
        BOOL InitSceneContext(Scene* scene);

        void RenderScene(float timestep);
        void RenderSceneRaytraced(float timestep);
        void Resize(UINT width, UINT height);

        UINT GetWidth() const { return m_swapchain.GetWidth(); }
        UINT GetHeight() const { return m_swapchain.GetHeight(); }

    private:
        void PopulateCommandLists(UINT frameIndex, float timestep, Scene* scene);
        void SubmitCommandList(nri::ECommandContextType contextType, ID3D12CommandList* commandList, ID3D12Fence* fence, UINT fenceValue);

        // Synchronizes with in-flight, waits if needed
        // returns  frame index of the next frame (as a swapchain's backbuffer index)
        UINT NextFrame();
        void WaitForFrame(UINT frameIndex) const;
        void WaitForLastFrame() const;
        void WaitForFenceValue(UINT64 fenceValue) const;

        HWND m_hwnd = nullptr;
        Scene* m_scene = nullptr;

        nri::ShaderCompiler m_shaderCompiler;
        nri::Swapchain m_swapchain;
        nri::DepthStencilBuffer m_depthStencilBuffer;
        nri::D3D12Rc<ID3D12Fence> m_fence;
        UINT m_frameIndex = 0;
        UINT64 m_fenceValues[NumInflightFrames];

        void InitCommandList();
        nri::D3D12Rc<ID3D12GraphicsCommandList4> m_commandList;

        enum ERendererRoots
        {
            eRendererRoots_InstanceInfo = 0,
            eRendererRoots_MaterialTextures,
            eRendererRoots_NumRoots,
        };

        void InitRootSignatureAndShaders();
        nri::RootSignature m_rootSignature;
        nri::Shader m_vsBasic;
        nri::Shader m_psBasic;

        void InitPipelineState();
        nri::D3D12Rc<ID3D12PipelineState> m_pipelineState;

        void InitInstanceCb();
        nri::ConstantBuffer m_cbInstance;

        RtScene m_raytracer;
    };

}