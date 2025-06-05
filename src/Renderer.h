#pragma once

#include "core/Scene.h"

#include "nri/ConstantBuffer.h"
#include "nri/DepthStencilBuffer.h"
#include "nri/RootSignature.h"
#include "nri/Shader.h"
#include "nri/Swapchain.h"

#include "DeferredRenderer.h"
#include "Raytracer.h"

#include <array>

namespace Neb
{

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

        // TODO: WIP here
        void RenderSceneDeferred(float timestep);
        void RenderUI(UINT frameIndex);

        void RenderScene(float timestep);
        void RenderSceneRaytraced(float timestep);
        void Resize(UINT width, UINT height);

        UINT GetWidth() const { return m_swapchain.GetWidth(); }
        UINT GetHeight() const { return m_swapchain.GetHeight(); }

        UINT GetFrameIndex() const { return m_frameIndex; }

    private:
        void SubmitCommandList(nri::ECommandContextType contextType, ID3D12CommandList* commandList, ID3D12Fence* fence, UINT fenceValue);

        void BeginCommandList(nri::ECommandContextType contextType);
        void EndCommandList(nri::ECommandContextType contextType, UINT frameIndex); // also updates fence value

        // Helper command that accepts a functor to be invoked as a part of command list scope
        template<typename Func, typename... Args>
        void ExecuteCommandList(nri::ECommandContextType contextType, UINT frameIndex, Func f, Args&&... args)
        {
            BeginCommandList(contextType);
            {
                std::invoke(f, std::forward<Args>(args)...);
            }
            EndCommandList(contextType, frameIndex);
        }

        ID3D12GraphicsCommandList4* GetCommandList() const { return m_commandList.Get(); }

        // Synchronizes with in-flight, waits if needed
        // returns  frame index of the next frame (as a swapchain's backbuffer index)
        UINT NextFrame();
        void WaitForFrame(UINT frameIndex) const;
        void WaitForLastFrame() const;
        void WaitForFenceValue(UINT64 fenceValue) const;

        HWND m_hwnd = nullptr;
        Scene* m_scene = nullptr;
        UINT m_frameIndex = 0;

        nri::Swapchain m_swapchain;
        nri::D3D12Rc<ID3D12Fence> m_fence;
        UINT m_backbufferIndex = 0;
        UINT64 m_fenceValues[NumInflightFrames];

        void InitCommandList();
        nri::D3D12Rc<ID3D12GraphicsCommandList4> m_commandList;
        nri::D3D12Rc<ID3D12CommandAllocator> m_currentCmdAllocator;

        // Deferred renderer is scene-agnostic, should be initialized in Init()
        DeferredRenderer m_deferredRenderer;

        // Opposed to deferred renderer RtScene should be initialized in InitSceneContext(), 
        // as it contains BLAS/TLAS that are dependant on the scene
        RtScene m_raytracer;

        void InitRtxgiContext(UINT width, UINT height, Scene* scene);
    };

}