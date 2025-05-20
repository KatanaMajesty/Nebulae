#pragma once

#include "core/Math.h"
#include "nri/stdafx.h"

namespace Neb::nri
{

    struct RTPrebuildInfo
    {
        UINT64 numScratchBytes;
        UINT64 numAccelerationStructureBytes;
        UINT64 numInstanceBufferBytes;
    };

    struct RTBlasBuffers
    {
        // validity only depends on the AS buffer, I dont think scratch buffer always matters
        BOOL IsValid() const { return accelerationStructureBuffer != NULL; }

        Rc<ID3D12Resource> scratchBuffer;
        Rc<ID3D12Resource> accelerationStructureBuffer;
    };

    struct RTTlasBuffers
    {
        // Scratch buffer does not affect validity after creation
        BOOL IsValid() const { return accelerationStructureBuffer != NULL && instanceDescriptorBuffer != NULL; }

        Rc<ID3D12Resource> scratchBuffer;
        Rc<ID3D12Resource> accelerationStructureBuffer;
        Rc<ID3D12Resource> instanceDescriptorBuffer;
    };

    struct RTBufferRange
    {
        RTBufferRange() = default;
        RTBufferRange(Rc<ID3D12Resource> buffer, UINT64 offsetInBytes, UINT elementStride, UINT numElements)
            : buffer(buffer)
            , offsetInBytes(offsetInBytes)
            , elementStride(elementStride)
            , numElements(numElements)
        {
        }

        template<typename T>
        static RTBufferRange Create(Rc<ID3D12Resource> buffer, UINT64 offsetInBytes, UINT numElements)
        {
            return RTBufferRange(buffer, offsetInBytes, sizeof(T), numElements);
        }

        Rc<ID3D12Resource> buffer;
        UINT64 offsetInBytes = 0;
        UINT elementStride = 0;
        UINT numElements = 0;
    };

    // Bottom level geometry represents information about the geometry to be submitted to DXR BLAS
    struct RTBottomLevelGeometry
    {
        inline D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC GetD3D12TrianglesDesc() const
        {
            D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC geometryDesc = {};
            geometryDesc.Transform3x4 = NULL; // we are not using transformation in BLAS

            NEB_ASSERT(positionBufferRange.elementStride == sizeof(Vec3), "We assume position format to be DXGI_FORMAT_R32G32B32_FLOAT");
            geometryDesc.VertexBuffer.StartAddress = positionBufferRange.buffer->GetGPUVirtualAddress() + positionBufferRange.offsetInBytes;
            geometryDesc.VertexBuffer.StrideInBytes = positionBufferRange.elementStride;
            geometryDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // just assert this for the entire Nebulae rt
            geometryDesc.VertexCount = positionBufferRange.numElements;

            geometryDesc.IndexBuffer = indexBufferRange.buffer->GetGPUVirtualAddress() + indexBufferRange.offsetInBytes;
            geometryDesc.IndexFormat = indexBufferRange.elementStride == sizeof(uint16_t) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            geometryDesc.IndexCount = indexBufferRange.numElements;
            return geometryDesc;
        }

        RTBufferRange positionBufferRange;
        RTBufferRange indexBufferRange;
    };

    // Represents TLAS instance data needed to initialize to initialize the acceleration structure
    struct RTTopLevelInstance
    {
        inline D3D12_RAYTRACING_INSTANCE_DESC GetD3D12InstanceDesc() const
        {
            D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
            std::memcpy(instanceDesc.Transform, &transformation, sizeof(D3D12_RAYTRACING_INSTANCE_DESC::Transform));
            instanceDesc.InstanceID = instanceID;
            instanceDesc.InstanceMask = 0xFF;
            instanceDesc.InstanceContributionToHitGroupIndex = hitGroupIndex;
            instanceDesc.Flags = flags;
            instanceDesc.AccelerationStructure = blasAccelerationStructure->GetGPUVirtualAddress();
            return instanceDesc;
        }

        Rc<ID3D12Resource> blasAccelerationStructure;
        Mat4 transformation;

        // only first 24 bits of instanceID are actually used, last 8 are for alignment
        UINT instanceID = UINT(-1);

        // only first 24 bits of hitGroupIndex are actually used, last 8 are for alignment
        UINT hitGroupIndex = UINT(-1);

        // TODO: Maybe have instance 'groups' to share those flags/hgIndex?
        D3D12_RAYTRACING_INSTANCE_FLAGS flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    };

}; // Neb::nri namespace