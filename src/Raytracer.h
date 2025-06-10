//#pragma once
//#pragma once
//
//#include "core/Math.h"
//#include "core/Scene.h"
//#include "nri/ConstantBuffer.h"
//#include "nri/DescriptorHeapAllocation.h"
//#include "nri/RootSignature.h"
//#include "nri/Shader.h"
//#include "nri/Swapchain.h"
//#include "nri/stdafx.h"
//
//// WIP: refactored raytracer
//#include "nri/raytracing/RTAccelerationStructureBuilder.h"
//#include "nri/raytracing/RTCommon.h"
//
//#include "DXRHelper/nv_helpers_dx12/ShaderBindingTableGenerator.h"
//
//#include <vector>
//#include <span>
//#include <filesystem>
//
//namespace Neb
//{
//
//    // TODO: for 27.07 -> https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-2
//    // DirectX raytracing (DXR) functional specification on GitHub: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#acceleration-structure-memory-restrictions
//    //
//    // for large-scene resource management bindless indexing should be used
//    // https://stackoverflow.com/questions/65794461/dxr-descriptor-heap-management-for-raytracing
//
//    // All CBs require 256 alignment
//    CONSTANT_BUFFER_STRUCT RtViewInfoCb
//    {
//        Mat4 ViewProjInverse;
//        Vec4 CameraWorldPos;
//    };
//
//    CONSTANT_BUFFER_STRUCT RtWorldInfoCb
//    {
//        // x,y,z - dir, w - 1D intensity
//        Vec4 dirLightDirectionAndIntensity;
//        Vec3 dirLightPosition; // just to play around
//    };
//
//    class RaytracingDescriptorArray
//    {
//        // CONCEPT:
//        // right now no need for material/attribute descriptors in the raytraced scene
//        // basically tlas-to-blas index mapping
//        // 
//        // Q: do we want to store all the material descriptors for instances in a single place?
//        // A1: probably not? because mesh' (and submesh') descriptors are stored separately from each other and its a mess honestly
//        //
//        // S1: create separete (new) descriptors for the textures that are actually needed and are used -> so it would make 2 SRVs for the same Normal Map for instance
//        //      -> thus need to create a container of ALL the textures that are used in the scene! (not per-instance or per-mesh containers)
//        //      -> the philosophy would be to store EXACTLY ONE extra descriptor per BLAS (per actual submesh)
//        //              -> the problem with this would be that there might be several instances of the same BLAS which would mess up instance ID addressing
//        //                 so we cannot use instance IDs to map to those extra descriptors
//        //              S2: instead of that we would have 1 more separate buffer to map from instance ID to correct index in the descriptor heap
//    };
//
//    // How to get vertex data other than positions: https://www.gamedev.net/forums/topic/709764-how-to-get-vertex-data-other-than-positions/
//    // https://github.com/TheRealMJP/DXRPathTracer/blob/master/DXRPathTracer/RayTrace.hlsl#L447
//
//    
//
//    // render the 'raytraced scene'
//    class Raytracer
//    {
//
//    };
//
//    class RtScene
//    {
//    public:
//        RtScene() = default;
//
//        BOOL Init(nri::Swapchain* swapchain);
//        BOOL InitSceneContext(ID3D12GraphicsCommandList4* commandList, Scene* scene);
//        void InitStaticMesh(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh);
//
//        void Resize(UINT width, UINT height);
//        void PopulateCommandLists(ID3D12GraphicsCommandList4* commandList, UINT frameIndex, float timestep);
//
//    private:
//        Scene* m_scene = nullptr;
//        nri::Swapchain* m_swapchain = nullptr;
//
//        // TODO: Only works with 1 TLAS -> support more in future
//        BOOL InitAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh);
//        BOOL UpdateAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh, float timestep);
//        Vec3 m_currentRotation = Vec3(90.0f, 0.0f, 0.0f);
//
//        nri::RTAccelerationStructureBuilder m_asBuilder;
//        nri::RTBlasBuffers m_blas;
//        nri::RTTlasBuffers m_tlas;
//
//        BOOL InitRaytracingPipeline();
//        nri::Rc<ID3D12StateObject> m_rtPso;
//        nri::Rc<ID3D12StateObjectProperties> m_rtPsoProperties;
//
//        enum EGlobalRoot
//        {
//            eGlobalRoot_CbViewInfo,
//            eGlobalRoot_CbWorldInfo,
//            eGlobalRoot_SrvTlas,
//            eGlobalRoot_NumRoots,
//        };
//
//        enum ERaygenRoot
//        {
//            eRaygenRoot_OutputUav = 0,
//            eRaygenRoot_NumRoots,
//        };
//
//        BOOL InitBasicShaders();
//        BOOL InitBasicShaderSignatures();
//        nri::Shader m_shaderBasic;
//        nri::RootSignature m_basicGlobalRS;
//        nri::RootSignature m_rayGenRS;
//        nri::RootSignature m_rayClosestHitRS;
//        nri::RootSignature m_rayMissRS;
//        nri::RootSignature m_shadowHitRS;
//        nri::RootSignature m_shadowMissRS;
//
//        BOOL InitResourcesAndDescriptors(UINT width, UINT height);
//        BOOL InitResources(UINT width, UINT height);
//        nri::Rc<ID3D12Resource> m_outputBuffer;
//
//        enum EDescriptorSlot
//        {
//            eDescriptorSlot_OutputBufferUav = 0,
//            eDescriptorSlot_TlasSrv,
//            eDescriptorSlot_NumSlots,
//        };
//        nri::DescriptorHeapAllocation m_rtDescriptors; // see EDescriptorSlot enum
//
//        void InitConstantBuffers();
//        nri::ConstantBuffer m_cbViewInfo;
//        nri::ConstantBuffer m_cbWorldInfo;
//        RtWorldInfoCb m_worldInfo;
//
//        BOOL InitShaderBindingTable();
//        nv_helpers_dx12::ShaderBindingTableGenerator m_sbtGenerator;
//        nri::Rc<ID3D12Resource> m_sbtBuffer;
//    };
//
//} // Neb namespace