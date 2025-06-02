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
        Mat4 InstanceToWorld;
        Mat4 ViewProj;
        uint32_t MaterialFlags;
    };

    CONSTANT_BUFFER_STRUCT CbViewData
    {
        Mat4 viewInv;
        Mat4 projInv;
    };

    CONSTANT_BUFFER_STRUCT CbLightEnvironment
    {
        float intensity;
        Vec3 direction;
        Vec3 radiance;
    };

    class DeferredRenderer
    {
    public:
        DeferredRenderer() = default;

        struct InitDesc
        {
            nri::Swapchain* swapchain = nullptr;
        };
        
        bool Init(UINT width, UINT height, nri::Swapchain* swapchain);
        void Resize(UINT width, UINT height);

        ID3D12Resource* GetGbufferAlbedo() const { return m_gbufferAlbedo->GetResource(); }
        ID3D12Resource* GetGbufferNormal() const { return m_gbufferNormal->GetResource(); }
        ID3D12Resource* GetGbufferRoughnessMetalness() const { return m_gbufferRoughnessMetalness->GetResource(); }
        ID3D12Resource* GetGbufferWorldPos() const { return m_gbufferWorldPos->GetResource(); }

        nri::DepthStencilBuffer& GetDepthStencilBuffer() { return m_depthStencilBuffer; }
        const nri::DepthStencilBuffer& GetDepthStencilBuffer() const { return m_depthStencilBuffer; }
        
        ID3D12Resource* GetHDROutputResource() const { return m_hdrResult->GetResource(); }

        // Performs deferred rendering into a swapchain provided in InitDesc
        struct RenderInfo
        {
            Scene* scene;
            ID3D12GraphicsCommandList4* commandList;
            UINT frameIndex;
            float timestep;
        };
        void SubmitCommandsGbuffer(const RenderInfo& info);
        void SubmitCommandsPBRLighting(const RenderInfo& info);
        void SubmitCommandsHDRTonemapping(ID3D12GraphicsCommandList4* commandList);

        void TransitionGbuffers(ID3D12GraphicsCommandList4* commandList,
            D3D12_RESOURCE_STATES prev,
            D3D12_RESOURCE_STATES next,
            D3D12_RESOURCE_STATES depthPrev,
            D3D12_RESOURCE_STATES depthNext);
        void SetupDescriptorHeaps(ID3D12GraphicsCommandList4* commandList);
        void SetupGbufferRtvs(ID3D12GraphicsCommandList4* commandList);
        void SetupViewports(ID3D12GraphicsCommandList4* commandList);

    private:
        bool m_isInitialized = false;
        UINT m_width = 0;
        UINT m_height = 0;
        nri::Swapchain* m_swapchain = nullptr;

        void InitGbuffers();
        void InitGbufferHeaps();
        void InitGbufferDepthStencilBuffer();
        void InitGbufferDepthSrv();
        void InitGbufferShadersAndRootSignatures();
        void InitGbufferPipelineState();
        void InitGbufferInstanceCb();
        
        nri::Rc<D3D12MA::Allocation> m_gbufferAlbedo;
        nri::Rc<D3D12MA::Allocation> m_gbufferNormal;
        nri::Rc<D3D12MA::Allocation> m_gbufferRoughnessMetalness;
        nri::Rc<D3D12MA::Allocation> m_gbufferWorldPos;
        enum EGbufferSlot
        {
            GBUFFER_SLOT_ALBEDO = 0,
            GBUFFER_SLOT_NORMAL = 1,
            GBUFFER_SLOT_ROUGHNESS_METALNESS = 2,
            GBUFFER_SLOT_WORLD_POS = 3,
            GBUFFER_SLOT_NUM_SLOTS, // represents the number of gbuffers (and the number of descriptors in the heap allocation)
        };
        nri::DescriptorHeapAllocation m_gbufferSrvHeap;
        nri::DescriptorHeapAllocation m_gbufferRtvHeap;
        nri::DepthStencilBuffer m_depthStencilBuffer;
        nri::DescriptorHeapAllocation m_depthSrv;
        enum EDeferredRendererRoots
        {
            DEFERRED_RENDERER_ROOTS_INSTANCE_INFO = 0,
            DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES = 1,
            DEFERRED_RENDERER_ROOTS_NUM_ROOTS,
        };
        nri::RootSignature m_gbufferRS;
        nri::Shader m_vsGbuffer;
        nri::Shader m_psGbuffer;
        nri::Rc<ID3D12PipelineState> m_pipelineState;
        nri::ConstantBuffer m_cbInstance;

        void InitPBRResources();
        void InitPBRDescriptorHeaps();
        void InitPBRShadersAndRootSignature();
        void InitPBRConstantBuffers();
        void InitPBRPipeline();

        nri::Rc<D3D12MA::Allocation> m_hdrResult;
        enum EHdrViewSlot // represents index of SrvUav heap where respective Srv/Uav is stored
        {
            HDR_SRV_INDEX = 0,
            HDR_UAV_INDEX = 1,
        };
        nri::DescriptorHeapAllocation m_pbrSrvUavHeap;
        enum EPbrRoots
        {
            PBR_ROOT_CB_VIEW_DATA = 0,
            PBR_ROOT_CB_LIGHT_ENV,
            PBR_ROOT_GBUFFERS,
            PBR_ROOT_SCENE_DEPTH,
            PBR_ROOT_HDR_OUTPUT_UAV,
            PBR_ROOT_NUM_ROOTS,
        };
        nri::RootSignature m_pbrRS;
        //nri::Shader m_vsPBR;
        nri::Shader m_csPBR;
        nri::ConstantBuffer m_cbViewData;
        nri::ConstantBuffer m_cbLightEnv;
        nri::Rc<ID3D12PipelineState> m_pbrPipeline;

        void InitHDRTonemapShadersAndRootSignature();
        void InitHDRTonemapPipeline(DXGI_FORMAT outputFormat);

        enum ETonemapRoots
        {
            TONEMAP_ROOT_HDR_INPUT = 0,
            TONEMAP_ROOT_NUM_ROOTS,
        };
        nri::RootSignature m_tonemapRS;
        nri::Shader m_vsTonemap;
        nri::Shader m_psTonemap;
        nri::Rc<ID3D12PipelineState> m_tonemapPipeline;
    };

} // Neb namespace