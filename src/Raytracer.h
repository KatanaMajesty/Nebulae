#pragma once
#pragma once
#pragma once

#include "core/Math.h"
#include "core/Scene.h"
#include "nri/DescriptorHeapAllocation.h"
#include "nri/RootSignature.h"
#include "nri/Shader.h"
#include "nri/stdafx.h"

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

    // Here we make an assumption that the stride of a vertex in the buffer is 3 * float3
    struct RtBLASGeometryBuffer
    {
        nri::Rc<ID3D12Resource> Buffer;
        UINT VertexStride = 0;
        UINT VertexOffset = 0;
        UINT NumVertices = 0;
    }; // RtBLASGeometryBuffer struct

    struct RtTLASInstanceBuffer
    {
        nri::Rc<ID3D12Resource> ASBuffer;
        Mat4 Transformation;
    };

    class RtScene
    {
    public:
        RtScene() = default;

        BOOL InitForScene(UINT width, UINT height, Scene* scene);
        void WaitForGpuContext(); // Effectively, blocks until all ray tracing operations are done
        void Resize(UINT width, UINT height);

        void Render();

    private:
        Scene* m_scene = nullptr;

        // TODO: Currently only works with a single static mesh and basically is just a setter (kinda)
        //      add support for more static meshes
        void AddStaticMesh(const nri::StaticMesh& staticMesh);
        void NextFrame();

        void InitCommandList();
        nri::Rc<ID3D12GraphicsCommandList4> m_commandList;

        void InitASFences();
        void WaitForFenceCompletion(); // Effectively, blocks until all ray tracing operations are done
        nri::Rc<ID3D12Fence> m_ASFence;
        UINT m_fenceValue = 0;

        RtAccelerationStructureBuffers CreateBLAS(std::span<const RtBLASGeometryBuffer> geometryBuffers);
        RtAccelerationStructureBuffers CreateTLAS(std::span<const RtTLASInstanceBuffer> instanceBuffers);
        // TODO: Only works with 1 TLAS -> support more in future
        BOOL InitAccelerationStructure(const nri::StaticMesh& staticMesh);

        RtAccelerationStructureBuffers m_tlas;

        BOOL InitRaytracingPipeline();
        nri::Rc<ID3D12StateObject> m_rtPso;
        nri::Rc<ID3D12StateObjectProperties> m_rtPsoProperties;

        BOOL InitRayGen(const std::filesystem::path& filepath, nri::EShaderModel shaderModel = nri::EShaderModel::sm_6_5);
        nri::Shader m_rayGen;
        nri::RootSignature m_rayGenRS;

        BOOL InitRayClosestHit(const std::filesystem::path& filepath, nri::EShaderModel shaderModel = nri::EShaderModel::sm_6_5);
        nri::Shader m_rayClosestHit;
        nri::RootSignature m_rayClosestHitRS;

        BOOL InitRayMiss(const std::filesystem::path& filepath, nri::EShaderModel shaderModel = nri::EShaderModel::sm_6_5);
        nri::Shader m_rayMiss;
        nri::RootSignature m_rayMissRS;

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
    };

} // Neb namespace