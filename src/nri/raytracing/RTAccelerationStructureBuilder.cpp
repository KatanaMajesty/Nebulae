#include "RTAccelerationStructureBuilder.h"

#include "common/Assert.h"
#include "nri/Device.h"

#include "util/Memory.h"

#include <ranges>
#include <cstring>

namespace Neb::nri
{

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> RTAccelerationStructureBuilder::QueryGeometryDescArray(const StaticMesh& mesh) const
    {
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> result(mesh.Submeshes.size());

        // populate geometryDescArray with submesh geometry data
        for (auto [submesh, geometryDesc] : std::views::zip(mesh.Submeshes, result))
        {
            RTBottomLevelGeometry submeshGeometry;
            submeshGeometry.positionBufferRange = RTBufferRange::Create<Vec3>(
                submesh.AttributeBuffers[eAttributeType_Position],
                submesh.AttributeOffsets[eAttributeType_Position],
                submesh.NumVertices);

            submeshGeometry.indexBufferRange = RTBufferRange(
                submesh.IndexBuffer,
                submesh.IndicesOffset,
                submesh.IndicesStride,
                submesh.NumIndices);

            geometryDesc = D3D12_RAYTRACING_GEOMETRY_DESC{
                .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
                .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE, // TODO: revisit this? Should it always be opaque by default?
                .Triangles = submeshGeometry.GetD3D12TrianglesDesc()
            };
        }

        return result;
    }

    RTPrebuildInfo RTAccelerationStructureBuilder::GetPrebuildInfo(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs) const
    {
        NRIDevice& device = NRIDevice::Get();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
        device.GetD3D12Device()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

        switch (inputs.Type)
        {
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL:
        {
            return RTPrebuildInfo{
                .numScratchBytes = AlignUp(prebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                .numAccelerationStructureBytes = AlignUp(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                .numInstanceBufferBytes = 0
            };
        }
        case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL:
        {
            UINT64 numInstanceBufferBytes = AlignUp(inputs.NumDescs * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
            return RTPrebuildInfo{
                .numScratchBytes = AlignUp(prebuildInfo.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                .numAccelerationStructureBytes = AlignUp(prebuildInfo.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT),
                .numInstanceBufferBytes = numInstanceBufferBytes
            };
        }
        default: NEB_ASSERT(false, "Hit unknown D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE value?"); return RTPrebuildInfo();
        }
    }

    RTBlasBuffers RTAccelerationStructureBuilder::CreateBlas(ID3D12GraphicsCommandList4* commandList, std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometryArray) const
    {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = {}; // no D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE for BLAS - we only update TLAS
        inputs.NumDescs = static_cast<UINT>(geometryArray.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = geometryArray.data();

        // first create BLAS buffers for the provided geometry
        RTPrebuildInfo prebuildInfo = this->GetPrebuildInfo(inputs);
        RTBlasBuffers blas = this->CreateBlasBuffers(prebuildInfo);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc = {};
        blasDesc.DestAccelerationStructureData = blas.accelerationStructureBuffer->GetGPUVirtualAddress();
        blasDesc.Inputs = inputs;
        blasDesc.ScratchAccelerationStructureData = blas.scratchBuffer->GetGPUVirtualAddress();
        commandList->BuildRaytracingAccelerationStructure(&blasDesc, 0, NULL);

        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(blas.accelerationStructureBuffer.Get());
        commandList->ResourceBarrier(1, &barrier);

        return blas;
    }

    RTTlasBuffers RTAccelerationStructureBuilder::CreateTlas(ID3D12GraphicsCommandList4* commandList, std::span<const RTTopLevelInstance> instances, const RTTlasBuffers& updateTlas)
    {
        UINT numInstances = static_cast<UINT>(instances.size());
        NEB_ASSERT(numInstances > 0, "No instances was submitted for CreateTlas");

        // if update tlas is a valid TLAS then we assume that ONLY update is to be performed
        bool performUpdate = updateTlas.IsValid();

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.Flags = performUpdate ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        inputs.NumDescs = numInstances;

        // To support TLAS update ONLY create buffers if nothing is inside the 'updateTlas buffer'
        RTTlasBuffers tlas = performUpdate ? updateTlas : this->CreateTlasBuffers(GetPrebuildInfo(inputs));

        // Assign instance descs address right after!
        this->UpdateTlasInputs(&inputs, instances, tlas.instanceDescriptorBuffer);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc = {};
        tlasDesc.DestAccelerationStructureData = tlas.accelerationStructureBuffer->GetGPUVirtualAddress();
        tlasDesc.Inputs = inputs;
        tlasDesc.SourceAccelerationStructureData = updateTlas.IsValid() ? tlas.accelerationStructureBuffer->GetGPUVirtualAddress() : NULL;
        tlasDesc.ScratchAccelerationStructureData = tlas.scratchBuffer->GetGPUVirtualAddress();
        commandList->BuildRaytracingAccelerationStructure(&tlasDesc, 0, NULL);

        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.accelerationStructureBuffer.Get());
        commandList->ResourceBarrier(1, &barrier);

        return tlas;
    }

    RTBlasBuffers RTAccelerationStructureBuilder::CreateBlasBuffers(const RTPrebuildInfo& prebuildInfo) const
    {
        NEB_ASSERT(IsAligned(prebuildInfo.numScratchBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));
        NEB_ASSERT(IsAligned(prebuildInfo.numAccelerationStructureBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));

        NRIDevice& device = NRIDevice::Get();

        RTBlasBuffers result;

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT
        };

        // Create scratch buffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
            // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.scratchBuffer.GetAddressOf())));
        }

        // Create ASBuffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.numAccelerationStructureBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.accelerationStructureBuffer.GetAddressOf())));
        }

        return result;
    }

    RTTlasBuffers RTAccelerationStructureBuilder::CreateTlasBuffers(const RTPrebuildInfo& prebuildInfo) const
    {
        NEB_ASSERT(IsAligned(prebuildInfo.numScratchBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));
        NEB_ASSERT(IsAligned(prebuildInfo.numAccelerationStructureBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT));

        NRIDevice& device = NRIDevice::Get();

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_DEFAULT };

        RTTlasBuffers result;

        // Create scratch buffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
            // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.scratchBuffer.GetAddressOf())));
        }

        // Create ASBuffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.numAccelerationStructureBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.accelerationStructureBuffer.GetAddressOf())));
        }

        // Instance descriptor buffer
        {
            D3D12MA::ALLOCATION_DESC instanceAllocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(prebuildInfo.numInstanceBufferBytes, D3D12_RESOURCE_FLAG_NONE);

            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&instanceAllocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.instanceDescriptorBuffer.GetAddressOf())));
        }

        return result;
    }

    void RTAccelerationStructureBuilder::UpdateTlasInputs(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* pInputs,
        std::span<const RTTopLevelInstance> instances,
        Rc<ID3D12Resource> instanceDescriptorBuffer)
    {
        NEB_ASSERT(instanceDescriptorBuffer, "Instance buffer is invalid!");
        NEB_LOG_WARN_IF(!pInputs, "Always specify pInputs for safety!");
        if (pInputs)
        {
            pInputs->InstanceDescs = instanceDescriptorBuffer->GetGPUVirtualAddress();
            NEB_ASSERT(instances.size() == pInputs->NumDescs, "Input data differs from instance span data provided");
        }

        D3D12_RAYTRACING_INSTANCE_DESC* pRawInstanceDescArray = NULL;
        ThrowIfFailed(instanceDescriptorBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pRawInstanceDescArray)),
            "Could not map memory of instance buffer");

        std::span<D3D12_RAYTRACING_INSTANCE_DESC> rtInstanceContainer(pRawInstanceDescArray, instances.size());
        for (auto [DXRInstance, instance] : std::views::zip(rtInstanceContainer, instances))
        {
            NEB_ASSERT(instance.instanceID != UINT(-1) && instance.hitGroupIndex != UINT(-1));
            DXRInstance = instance.GetD3D12InstanceDesc();
        }

        instanceDescriptorBuffer->Unmap(0, nullptr);
    }

}; // Neb::nri namespace