#pragma once

#include "core/Math.h"
#include "nri/StaticMesh.h"
#include "nri/stdafx.h"
#include "nri/nvidia/NsightAftermathCrashTracker.h"

#include <vector>
#include <span>

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

        bool InitForStaticMesh(const nri::StaticMesh& staticMesh);

    private:
        // TODO: Currently only works with a single static mesh and basically is just a setter (kinda)
        //      add support for more static meshes
        void AddStaticMesh(const nri::StaticMesh& staticMesh);

        void InitCommandList();
        nri::Rc<ID3D12GraphicsCommandList4> m_commandList;

        void InitASFences();
        nri::Rc<ID3D12Fence> m_accelerationStructFence;
        UINT m_fenceValue = 0;

        RtAccelerationStructureBuffers CreateBLAS(std::span<const RtBLASGeometryBuffer> geometryBuffers);
        RtAccelerationStructureBuffers CreateTLAS(std::span<const RtTLASInstanceBuffer> instanceBuffers);
        // TODO: Only works with 1 TLAS -> support more in future
        bool InitAccelerationStructure(const nri::StaticMesh& staticMesh);

        RtAccelerationStructureBuffers m_tlas;
    };

} // Neb namespace