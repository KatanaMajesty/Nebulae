#pragma once

#include <cstddef>
#include <array>
#include <vector>

#include "stdafx.h"
#include "Material.h"
#include "../core/Math.h"

namespace Neb::nri
{

    struct StaticVertex
    {
        Neb::Vec3 Position;
        Neb::Vec3 Normal;
        Neb::Vec2 TexCoords;
    };

    struct StaticSubmesh
    {
        // TODO: Do not overcomplicate for now with SoA, just go for AoS for easier ray tracing
        // Just make it a plain mesh struct with no overdesigned methods for now
        std::vector<StaticVertex> Vertices;

        // Indices may be in uint16_t or uint32_t - we handle both cases here
        std::vector<std::byte> Indices;
        size_t IndicesStride;

        D3D12Rc<ID3D12Resource> VertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW VBView;

        D3D12Rc<ID3D12Resource> IndexBuffer;
        D3D12_INDEX_BUFFER_VIEW IBView;
    };

    struct StaticMesh
    {
        // Static mesh is pretty much an array of submeshes
        std::vector<StaticSubmesh> Submeshes;        
        std::vector<Material> SubmeshMaterials; // TODO: Maybe replace with proxies, figure out best way to cache
    };

    static constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 3> StaticMeshInputLayout = {
        D3D12_INPUT_ELEMENT_DESC
        {
            .SemanticName = "POSITION",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0,
            .AlignedByteOffset = offsetof(StaticVertex, Position),
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        },
        D3D12_INPUT_ELEMENT_DESC
        {
            .SemanticName = "NORMAL",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 1,
            .AlignedByteOffset = offsetof(StaticVertex, Normal),
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        },
        D3D12_INPUT_ELEMENT_DESC
        {
            .SemanticName = "TEX_COORDS",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32_FLOAT,
            .InputSlot = 2,
            .AlignedByteOffset = offsetof(StaticVertex, TexCoords),
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        }
    };

} // Neb::nri namespace