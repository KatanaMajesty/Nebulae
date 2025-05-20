#pragma once

#include "nri/raytracing/RTCommon.h"
#include "nri/StaticMesh.h"
#include "nri/stdafx.h"

#include <vector>
#include <span>

namespace Neb::nri
{

    class RTAccelerationStructureBuilder
    {
    public:
        RTAccelerationStructureBuilder() = default;

        // Pre-build BLAS info
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> QueryGeometryDescArray(const StaticMesh& mesh) const;
        RTPrebuildInfo GetPrebuildInfo(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& inputs) const;

        // remark: only support for TLAS updates now, BLAS are static
        RTBlasBuffers CreateBlas(ID3D12GraphicsCommandList4* commandList, std::span<const D3D12_RAYTRACING_GEOMETRY_DESC> geometryArray) const;
        RTTlasBuffers CreateTlas(ID3D12GraphicsCommandList4* commandList, std::span<const RTTopLevelInstance> instances, const RTTlasBuffers& updateTlas = RTTlasBuffers());

    private:
        RTBlasBuffers CreateBlasBuffers(const RTPrebuildInfo& prebuildInfo) const;
        RTTlasBuffers CreateTlasBuffers(const RTPrebuildInfo& prebuildInfo) const;

        // Copies instance data into instanceDescriptorBuffer for TLAS to use later, updates pInputs to point towards instanceDescriptorBuffer
        void UpdateTlasInputs(D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* pInputs, std::span<const RTTopLevelInstance> instances, Rc<ID3D12Resource> instanceDescriptorBuffer);
    };

}; // Neb::nri namespace