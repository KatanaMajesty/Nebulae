#pragma once

#include "core/Math.h"
#include "nri/StaticMesh.h"
#include "nri/Material.h"
#include "nri/stdafx.h"
#include "nri/DescriptorHeapAllocation.h"

#include <vector>
#include <span>
#include <unordered_map>

namespace Neb::nri
{
    static constexpr int32_t PathtracerInvalidBindlessIndex = -1;

    struct StaticMeshGeometryData
    {
        Mat4 surfaceToWorld;
        int32_t materialIndex = PathtracerInvalidBindlessIndex;

        int32_t indexBufferIndex = PathtracerInvalidBindlessIndex;
        uint32_t indexBufferOffset; // in bytes
        uint32_t indexBufferStride; // either 2 of 4 for uint32 or uint16
        uint32_t indexBufferSizeInbytes;

        int32_t attributeBufferIndices[eAttributeType_NumTypes];
        uint32_t attributeBufferOffsets[eAttributeType_NumTypes]; // in bytes
        uint32_t attributeBufferStrides[eAttributeType_NumTypes]; // in bytes
        uint32_t numVertices; // all attributes match this number of elements
    };

    struct StaticMeshMaterialData
    {
        int32_t textureIndices[eMaterialTextureType_NumTypes];

        Vec4 albedo;             // if no albedo texture available
        Vec2 roughnessMetalness; // if no roughness/metalness map available
    };

    struct GIBindlessBuffer
    {
        uint32_t GetSize() const { return static_cast<uint32_t>(resources.size()); }
        uint32_t GetNextResourceIndex() const { return static_cast<uint32_t>(resources.size()); }
        uint32_t AddResource(Rc<ID3D12Resource> resource) 
        {
            NEB_ASSERT(resource, "Resource cannot be null");

            ID3D12Resource* key = resource.Get();
            if (resourceIndexMap.contains(key))
                return resourceIndexMap[key];

            uint32_t resourceIndex = GetNextResourceIndex();
            resources.push_back(resource);
            resourceIndexMap.emplace(key, resourceIndex);
            return resourceIndex;
        }
        ID3D12Resource* At(uint32_t i) const { return resources.at(i).Get(); }

        std::vector<Rc<ID3D12Resource>> resources;
        std::unordered_map<ID3D12Resource*, uint32_t> resourceIndexMap; // Cache resource indices in 'resources' array before adding new entries
    };

    // This scene stores scene-related context needed for GI/pathtracing, such as:
    // - texture/buffer heaps used for bindless
    // - instance/geometry/material data needed for surface sampling during ray casting
    class GIProcessedScene
    {
    public:
        GIProcessedScene() = default;

        bool InitScene(std::span<const StaticMesh> staticMeshes, bool createResourceContext = true);
        bool IsInitialized() const { return !GetStaticMeshes().empty(); }

        std::span<const StaticMesh> GetStaticMeshes() const { return m_staticMeshes; }

        ID3D12Resource* GetGeometryDataBuffer() const { return m_geometryData.Get(); }
        ID3D12Resource* GetMaterialDataBuffer() const { return m_materialData.Get(); }

        const DescriptorHeapAllocation& GetGeometryDataHeap() const { return m_meshGeometryDataHeap; }
        const DescriptorHeapAllocation& GetMaterialDataHeap() const { return m_meshMaterialDataHeap; }
        const DescriptorHeapAllocation& GetBindlessBufferHeap() const { return m_bindlessBufferHeap; }
        const DescriptorHeapAllocation& GetBindlessTextureHeap() const { return m_bindlessTextureHeap; }

        const GIBindlessBuffer& GetBindlessBuffers() const { return m_bindlessBuffers; }

    private:
        // read-only view onto static meshes of the scene
        // for now in Nebulae this information should be enough to properly construct 
        // all descriptors/resources needed for pathtracing
        std::span<const StaticMesh> m_staticMeshes;

        std::vector<StaticMeshGeometryData> m_meshGeometries;
        std::vector<StaticMeshMaterialData> m_meshMaterials;

        // collected during scene processing
        GIBindlessBuffer m_bindlessBuffers;
        GIBindlessBuffer m_bindlessTextures;

        bool CreateResources();
        bool CreateAndUploadResources();
        void WaitResourcesOnHost();
        bool CreateDescriptors();

        // Using own allocators, command list, copy fence
        // same way as in GLTFSceneImporter - there is definitely a better way to go around it
        Rc<ID3D12GraphicsCommandList> m_stagingCommandList;
        Rc<ID3D12Fence> m_copyFence;
        UINT64 m_copyFenceValue = 0;

        std::vector<Rc<ID3D12Resource>> m_stagingResources;
        Rc<ID3D12Resource> m_geometryData;
        Rc<ID3D12Resource> m_materialData;
        DescriptorHeapAllocation m_meshGeometryDataHeap;
        DescriptorHeapAllocation m_meshMaterialDataHeap;
        DescriptorHeapAllocation m_bindlessBufferHeap;
        DescriptorHeapAllocation m_bindlessTextureHeap;
    };

} // Neb::nri namespace