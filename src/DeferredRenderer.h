#pragma once

#include "core/Scene.h"
#include "nri/stdafx.h"
#include "nri/ConstantBuffer.h"
#include "nri/Device.h"
#include "nri/DescriptorHeapAllocation.h"
#include "nri/DepthStencilBuffer.h"
#include "nri/Swapchain.h"
#include "nri/Shader.h"
#include "nri/RootSignature.h"

namespace Neb
{

    CONSTANT_BUFFER_STRUCT CbInstanceInfo
    {
        Neb::Mat4 InstanceToWorld;
        Neb::Mat4 ViewProj;
        uint32_t MaterialFlags;
    };

    class DeferredRenderer
    {
    public:
        DeferredRenderer() = default;

        struct InitDesc
        {
            nri::Swapchain* swapchain = nullptr;
        };
        
        bool Init(UINT width, UINT height);
        void Resize(UINT width, UINT height);

        ID3D12Resource* GetGbufferAlbedo() const { return m_gbufferAlbedo->GetResource(); }
        ID3D12Resource* GetGbufferNormal() const { return m_gbufferNormal->GetResource(); }
        ID3D12Resource* GetGbufferRoughnessMetalness() const { return m_gbufferRoughnessMetalness->GetResource(); }

        nri::DepthStencilBuffer& GetDepthStencilBuffer() { return m_depthStencilBuffer; }
        const nri::DepthStencilBuffer& GetDepthStencilBuffer() const { return m_depthStencilBuffer; }
        
        struct RenderInfo
        {
            Scene* scene;
            ID3D12GraphicsCommandList4* commandList;
            UINT frameIndex;
            float timestep;
        };

        // Performs deferred rendering into a swapchain provided in InitDesc
        void SubmitCommands(const RenderInfo& info);

    private:
        bool m_isInitialized = false;
        UINT m_width = 0;
        UINT m_height = 0;

        void InitGbuffers();
        nri::Rc<D3D12MA::Allocation> m_gbufferAlbedo;
        nri::Rc<D3D12MA::Allocation> m_gbufferNormal;
        nri::Rc<D3D12MA::Allocation> m_gbufferRoughnessMetalness;

        void InitGbufferHeaps();
        enum EGbufferSlot
        {
            GBUFFER_SLOT_ALBEDO = 0,
            GBUFFER_SLOT_NORMAL = 1,
            GBUFFER_SLOT_ROUGHNESS_METALNESS = 2,
            GBUFFER_SLOT_NUM_SLOTS, // represents the number of gbuffers (and the number of descriptors in the heap allocation)
        };
        nri::DescriptorHeapAllocation m_gbufferSrvHeap;
        nri::DescriptorHeapAllocation m_gbufferRtvHeap;

        void InitDepthStencilBuffer();
        nri::DepthStencilBuffer m_depthStencilBuffer;

        void InitShadersAndRootSignatures();
        enum EDeferredRendererRoots
        {
            DEFERRED_RENDERER_ROOTS_INSTANCE_INFO = 0,
            DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES = 1,
            DEFERRED_RENDERER_ROOTS_NUM_ROOTS,
        };
        nri::RootSignature m_gbufferRS;
        nri::Shader m_vsGbuffer;
        nri::Shader m_psGbuffer;

        void InitPipelineState();
        nri::Rc<ID3D12PipelineState> m_pipelineState;

        void InitInstanceCb();
        nri::ConstantBuffer m_cbInstance;
    };

} // Neb namespace