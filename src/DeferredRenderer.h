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
#include "nri/raytracing/RTAccelerationStructureBuilder.h"
#include "nri/raytracing/RTCommon.h"
#include "nri/nvidia/NvRtxgiNRC.h"
#include "nri/GIProcessedScene.h"

#include "DXRHelper/nv_helpers_dx12/ShaderBindingTableGenerator.h"

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
        Vec3 direction;
        float tanHalfAngle; // tan(0.265) = 4.63e-3
        Vec3 radiance; // linear RGB, W * sr^-1 * m^-2
        UINT frameIndex;
    };

    class DeferredRenderer
    {
    public:
        DeferredRenderer() = default;

        bool Init(UINT width, UINT height, nri::Swapchain* swapchain);
        void Resize(UINT width, UINT height);

        // Frame index is an always incremental ID of the frame, not the swapchain index
        struct RenderInfo
        {
            Scene* scene;
            ID3D12GraphicsCommandList4* commandList;
            UINT backbufferIndex;
            UINT frameIndex;
            float timestep;
        };
        void BeginFrame(const RenderInfo& info);
        void EndFrame();

        ID3D12Resource* GetGbufferAlbedo() const { return m_gbufferAlbedo->GetResource(); }
        ID3D12Resource* GetGbufferNormal() const { return m_gbufferNormal->GetResource(); }
        ID3D12Resource* GetGbufferRoughnessMetalness() const { return m_gbufferRoughnessMetalness->GetResource(); }
        ID3D12Resource* GetGbufferWorldPos() const { return m_gbufferWorldPos->GetResource(); }

        nri::DepthStencilBuffer& GetDepthStencilBuffer() { return m_depthStencilBuffer; }
        const nri::DepthStencilBuffer& GetDepthStencilBuffer() const { return m_depthStencilBuffer; }
        
        ID3D12Resource* GetHDROutputResource() const { return m_hdrResult->GetResource(); }

        void SubmitUICommands();
        void SubmitCommandsGbuffer();
        void SubmitCommandsPBRLighting();
        void SubmitCommandsGIPathtrace();
        void SubmitCommandsHDRTonemapping(ID3D12GraphicsCommandList4* commandList);

        void TransitionGbuffers(ID3D12GraphicsCommandList4* commandList,
            D3D12_RESOURCE_STATES prev,
            D3D12_RESOURCE_STATES next,
            D3D12_RESOURCE_STATES depthPrev,
            D3D12_RESOURCE_STATES depthNext);
        void SetupDescriptorHeaps(ID3D12GraphicsCommandList4* commandList);
        void SetupGbufferRtvs(ID3D12GraphicsCommandList4* commandList);
        void SetupViewports(ID3D12GraphicsCommandList4* commandList);

        NrcConstants& GetNrcConstants() { return m_nrcConstants; }
        const NrcConstants& GetNrcConstants() const { return m_nrcConstants; }

        Scene* GetCurrentScene() const { return m_scene; }

    private:
        UINT m_width = 0;
        UINT m_height = 0;
        nri::Swapchain* m_swapchain = nullptr;
        RenderInfo m_renderInfo = RenderInfo();

        UINT m_frameIndex;

        struct SceneSunUI
        {
            float roughDiameter = 0.58f; // Rough estimate of sun diameter as seen from Earth
            Vec3 direction = Vec3(0.66f, -1.0f, -0.2f);
            Vec3 radiance = Vec3(2.0f, 1.94f, 1.9f);
        } m_sceneSunUI;

        void InitGbuffers();
        void InitGbufferHeaps();
        void InitGbufferDepthStencilBuffer();
        void InitGbufferDepthStencilSrv();
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
        nri::DescriptorHeapAllocation m_depthStencilSrvHeap; // depth at index 0, stencil at index 1
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
            PBR_ROOT_SCENE_STENCIL,
            PBR_ROOT_SCENE_TLAS_SRV,
            PBR_ROOT_HDR_OUTPUT_UAV,
            PBR_ROOT_NUM_ROOTS,
        };
        nri::RootSignature m_pbrRS;
        nri::Shader m_csPBR;
        nri::ConstantBuffer m_cbViewData;
        nri::ConstantBuffer m_cbLightEnv;
        nri::Rc<ID3D12PipelineState> m_pbrPipeline;

        // These ones are a bit special as they are called inside command submition lazily
        void InitRTAccelerationStructures(ID3D12GraphicsCommandList4* commandList);

        // Ray-tracing objects for inline ray queries during PBR lighting
        //      usage may expand soon
        Scene* m_scene = nullptr;
        nri::RTAccelerationStructureBuilder m_asBuilder;
        nri::RTBlasBuffers m_blas;
        nri::RTTlasBuffers m_tlas;
        nri::DescriptorHeapAllocation m_tlasSrvHeap;
        bool m_needsASUpdate = false;

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

        static constexpr uint32_t MaxPathtracingRecursionDepth = 8;
        struct GlobalIlluminationUI
        {
            Vec3 skyColor = Vec3(0.58, 0.79f, 0.95f);
            int32_t giSamplesPerPixel = 1;
            int32_t nrcMaxPathVertices = MaxPathtracingRecursionDepth;
            float throughputThreshold = 0.01f;
        } m_globalIlluminationUI;

        // returns true if NRC was re-configured, otherwise false
        bool ConfigureNRCState(const nrc::ContextSettings& nrcContextSettings);
        void InitPathtracerScene(Scene* scene); // scene is specified explicitly to allow for independent re-configurations of heaps
        void InitPathtracerDescriptors();
        void InitPathtracerShadersAndRootSignatures();
        void InitPathtracerPipeline();
        void InitPathtracerSBT();
        void InitPathtracerConstantBuffers();
        void InitPathtracerNRCQueryDebugResources(UINT width, UINT height);

        CONSTANT_BUFFER_STRUCT GlobalConstants
        {
            // https://maraneshi.github.io/HLSL-ConstantBufferLayoutVisualizer/
            uint32_t frameIndex;
            uint32_t samplesPerPixel;
            Vec2 nrcTrainingDownscale;

            uint32_t nrcMaxPathVertices;
            Vec3 cameraWorldPos;

            Vec3 skyColor;
            uint32_t _pad0;

            Vec3 sunLightDirection;
            uint32_t _pad1;

            Vec3 sunLightRadiance;
            float sunTanHalfAngle;
            float throughputThreshold;
        };

        nri::GIProcessedScene m_giScene;
        nri::DescriptorHeapAllocation m_nrcBufferUavHeap;

        NrcConstants m_nrcConstants;
        GlobalConstants m_globalConstants;
        nri::ConstantBuffer m_nrcConstantsCB;
        nri::ConstantBuffer m_globalConstantsCB;

        nrc::ContextSettings m_nrcContextSettings;
        nri::Rc<ID3D12StateObject> m_nrcUpdatePSO;
        nri::Rc<ID3D12StateObject> m_nrcQueryPSO;
        nri::Shader m_rsUpdatePathtracer;
        nri::Shader m_rsQueryPathtracer;
        enum EPathtracerRoots
        {
            PATHTRACER_ROOT_NRC_CONSTANTS = 0,
            PATHTRACER_ROOT_GLOBAL_CONSTANTS,
            PATHTRACER_ROOT_NRC_BUFFERS,
            PATHTRACER_ROOT_NRC_NEBULAE_BUFFERS,
            PATHTRACER_ROOT_GBUFFER_TEXTURES,
            PATHTRACER_ROOT_SCENE_DEPTH,
            PATHTRACER_ROOT_SCENE_STENCIL,
            PATHTRACER_ROOT_SCENE_BVH,
            PATHTRACER_ROOT_BINDLESS_TEXTURES,
            PATHTRACER_ROOT_BINDLESS_BUFFERS,
            PATHTRACER_ROOT_GEOMETRY_DATA,
            PATHTRACER_ROOT_MATERIAL_DATA,
            PATHTRACER_ROOT_NUM_ROOTS
        };
        nri::RootSignature m_giGlobalRS;
        nri::RootSignature m_giRayGenRS;
        nri::RootSignature m_giRayClosestHitRS;
        nri::RootSignature m_giRayMissRS;
        
        nv_helpers_dx12::ShaderBindingTableGenerator m_rsUpdateSBTGenerator;
        nv_helpers_dx12::ShaderBindingTableGenerator m_rsQuerySBTGenerator;
        nri::Rc<ID3D12Resource> m_rsUpdateSBTBuffer;
        nri::Rc<ID3D12Resource> m_rsQuerySBTBuffer;

        enum ENRCNebulaeDebugBuffers
        {
            NRC_NEB_DEBUG_BUFFER_QUERY_THROUGHPUT_MAP = 0,
            NRC_NEB_DEBUG_BUFFER_QUERY_HIT_MAP,
            NRC_NEB_DEBUG_BUFFER_NUM_BUFFERS
        };
        nri::Rc<ID3D12Resource> m_NRCDebugQueryThroughputMap;
        nri::Rc<ID3D12Resource> m_NRCDebugQueryHitMap;
        nri::DescriptorHeapAllocation m_NRCDebugBuffersHeap;

        // this cannot be called before NvRtxgiNRC integration context was initialized
        void InitRadianceResolveShadersAndPSO();
        void InitRadianceResolveCreateResourcesAndDescriptors();

        enum ERadianceResolveRoots
        {
            RADIANCE_RESOLVE_ROOT_NRC_CONSTANTS = 0,
            RADIANCE_RESOLVE_ROOT_SCREEN_CONSTANTS,
            RADIANCE_RESOLVE_ROOT_NRC_BUFFERS,
            RADIANCE_RESOLVE_ROOT_HDR_OUTPUT,
            RADIANCE_RESOLVE_ROOT_NUM_ROOTS,
        };
        nri::Shader m_csRadianceResolve;
        nri::RootSignature m_radianceResolveRS;
        nri::Rc<ID3D12PipelineState> m_radianceResolvePSO;
        nri::DescriptorHeapAllocation m_radianceResolveNrcSrvHeap; // 0 - PackedPathInfo, 1 - PackedRadiance
    };

} // Neb namespace