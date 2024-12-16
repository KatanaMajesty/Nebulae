#pragma once

#include <cstddef>
#include <array>
#include <vector>

#include "stdafx.h"
#include "Material.h"
#include "../core/Math.h"

namespace Neb::nri
{

    enum EAttributeType
    {
        eAttributeType_Position = 0,
        eAttributeType_Normal,
        eAttributeType_TexCoords,
        eAttributeType_Tangents,
        eAttributeType_NumTypes
    };

    struct StaticSubmesh
    {
        // Decided to go for SoA instead AoS, because its natively in glTF and is easier to parse that way
        UINT NumVertices = 0;
        // sometimes all the attributes are stored in the same buffer and thus AttributeBuffers will all point to the same address
        // to determine correct offset into the buffer use AttributeOffsets and AttributeStrides
        std::array<UINT, eAttributeType_NumTypes> AttributeStrides = {};
        std::array<size_t, eAttributeType_NumTypes> AttributeOffsets = {}; // in bytes
        std::array<std::vector<std::byte>, eAttributeType_NumTypes> Attributes;

        std::array<D3D12Rc<ID3D12Resource>, eAttributeType_NumTypes> AttributeBuffers;
        std::array<D3D12_VERTEX_BUFFER_VIEW, eAttributeType_NumTypes> AttributeViews = {};

        // Indices may be in uint16_t or uint32_t - we handle both cases here
        UINT NumIndices = 0;
        UINT IndicesStride = 0;
        size_t IndicesOffset = 0; // in bytes
        std::vector<std::byte> Indices;

        D3D12Rc<ID3D12Resource> IndexBuffer;
        D3D12_INDEX_BUFFER_VIEW IBView = {};
    };

    struct StaticMesh
    {
        // Static mesh is pretty much an array of submeshes
        std::vector<StaticSubmesh> Submeshes;
        std::vector<Material> SubmeshMaterials; // TODO: Maybe replace with proxies, figure out best way to cache
    };

    static constexpr std::array StaticMeshInputLayout = {
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "POSITION",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "NORMAL",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 1,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "TEXCOORD",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32_FLOAT,
            .InputSlot = 2,
        },
        D3D12_INPUT_ELEMENT_DESC{
            .SemanticName = "TANGENT",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
            .InputSlot = 3,
        }
    };

} // Neb::nri namespace