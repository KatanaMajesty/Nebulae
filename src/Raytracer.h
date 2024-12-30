#pragma once
#pragma once

#include "core/Math.h"
#include "core/Scene.h"
#include "nri/ConstantBuffer.h"
#include "nri/DescriptorHeapAllocation.h"
#include "nri/RootSignature.h"
#include "nri/Shader.h"
#include "nri/Swapchain.h"
#include "nri/stdafx.h"

#include "DXRHelper/nv_helpers_dx12/ShaderBindingTableGenerator.h"

#include <vector>
#include <span>
#include <filesystem>

namespace Neb
{

    // TODO: for 27.07 -> https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial-Part-2
    // DirectX raytracing (DXR) functional specification on GitHub: https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#acceleration-structure-memory-restrictions

    // TODO: I dont think we need that structure to be generic for BLAS/TLAS as in former we only care about ASBuffer
    //      Probably rethink that and rename to smth like RtTLASBuffers?
    struct RtAccelerationStructureBuffers
    {
        nri::Rc<ID3D12Resource> ScratchBuffer;
        nri::Rc<ID3D12Resource> ASBuffer; // the actual buffer to hold AS
        nri::Rc<ID3D12Resource> InstanceDescriptorBuffer;
    }; // RTAccelerationStructureBuffers struct

    // All CBs require 256 alignment
    struct alignas(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
        RtViewInfoCb
    {
        Mat4 ViewProjInverse;
        Vec4 CameraWorldPos;
    };

    struct alignas(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)
        RtWorldInfoCb
    {
        // x,y,z - dir, w - 1D intensity
        Vec4 dirLightDirectionAndIntensity;
        Vec3 dirLightPosition; // just to play around
    };

    // Here we make an assumption that the stride of a vertex in the buffer is 3 * float3
    struct RtBLASGeometryBuffer
    {
        nri::Rc<ID3D12Resource> PositionBuffer;
        UINT VertexStride = 0;
        UINT64 VertexOffsetInBytes = 0;
        UINT NumVertices = 0;

        nri::Rc<ID3D12Resource> IndexBuffer;
        UINT IndexStride = 0;
        UINT64 IndexOffsetInBytes = 0;
        UINT NumIndices = 0; // assumed to be uint32_t indices
    };                       // RtBLASGeometryBuffer struct

    struct RtTLASInstanceBuffer
    {
        nri::Rc<ID3D12Resource> ASBuffer;
        Mat4 Transformation;
    };

    class RtScene
    {
    public:
        RtScene() = default;

        BOOL Init(nri::Swapchain* swapchain);
        BOOL InitSceneContext(ID3D12GraphicsCommandList4* commandList, Scene* scene);
        void InitStaticMesh(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh);

        void Resize(UINT width, UINT height);
        void PopulateCommandLists(ID3D12GraphicsCommandList4* commandList, UINT frameIndex, float timestep);

    private:
        Scene* m_scene = nullptr;
        nri::Swapchain* m_swapchain = nullptr;

        RtAccelerationStructureBuffers CreateBLAS(ID3D12GraphicsCommandList4* commandList, std::span<const RtBLASGeometryBuffer> geometryBuffers);
        RtAccelerationStructureBuffers CreateTLAS(
            ID3D12GraphicsCommandList4* commandList,
            std::span<const RtTLASInstanceBuffer> instanceBuffers,
            const RtAccelerationStructureBuffers& updateTlas = RtAccelerationStructureBuffers());

        // TODO: Only works with 1 TLAS -> support more in future
        BOOL InitAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh);
        BOOL UpdateAccelerationStructure(ID3D12GraphicsCommandList4* commandList, float timestep);
        Vec3 m_currentRotation = Vec3(90.0f, 0.0f, 0.0f);

        RtAccelerationStructureBuffers m_blas;
        RtAccelerationStructureBuffers m_tlas;

        BOOL InitRaytracingPipeline();
        nri::Rc<ID3D12StateObject> m_rtPso;
        nri::Rc<ID3D12StateObjectProperties> m_rtPsoProperties;

        enum EGlobalRoot
        {
            eGlobalRoot_CbViewInfo,
            eGlobalRoot_CbWorldInfo,
            eGlobalRoot_SrvTlas,
            eGlobalRoot_NumRoots,
        };

        enum ERaygenRoot
        {
            eRaygenRoot_OutputUav = 0,
            eRaygenRoot_NumRoots,
        };

        BOOL InitBasicShaders();
        BOOL InitBasicShaderSignatures();
        nri::Shader m_shaderBasic;
        nri::RootSignature m_basicGlobalRS;
        nri::RootSignature m_rayGenRS;
        nri::RootSignature m_rayClosestHitRS;
        nri::RootSignature m_rayMissRS;
        nri::RootSignature m_shadowHitRS;
        nri::RootSignature m_shadowMissRS;

        BOOL InitResourcesAndDescriptors(UINT width, UINT height);
        BOOL InitResources(UINT width, UINT height);
        nri::Rc<ID3D12Resource> m_outputBuffer;

        enum EDescriptorSlot
        {
            eDescriptorSlot_OutputBufferUav = 0,
            eDescriptorSlot_TlasSrv,
            eDescriptorSlot_NumSlots,
        };
        nri::DescriptorHeapAllocation m_rtDescriptors; // see EDescriptorSlot enum

        void InitConstantBuffers();
        nri::ConstantBuffer m_cbViewInfo;
        nri::ConstantBuffer m_cbWorldInfo;
        RtWorldInfoCb m_worldInfo;

        BOOL InitShaderBindingTable();
        nv_helpers_dx12::ShaderBindingTableGenerator m_sbtGenerator;
        nri::Rc<ID3D12Resource> m_sbtBuffer;
    };

} // Neb namespace