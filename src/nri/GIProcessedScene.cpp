#include "GIProcessedScene.h"

#include "nri/DescriptorHeap.h"
#include "nri/Device.h"

#include "common/Log.h"
#include "util/File.h"

#include <span>
#include <format>
#include <cstddef>

namespace Neb::nri
{

    bool GIProcessedScene::InitScene(std::span<const StaticMesh> staticMeshes, bool createResourceContext)
    {
        if (staticMeshes.empty())
        {
            NEB_LOG_ERROR("GIProcessedScene -> static meshes span is empty");
            return false;
        }

        // setup the state
        m_staticMeshes = staticMeshes;
        m_meshGeometries.clear();
        m_meshMaterials.clear();

        // - - - - - - - - - - - - - - - 
        // Nebulae handles RT meshes a bit differently due to deadlines
        // 
        // TLDR:
        // MATCH THIS: Instead of properly handling InstanceIDs and TLAS we just pack the entire scene into a single scene:
        //      thus -> only 1 InstanceID() value possible (see RTTopLevelInstance::GetD3D12InstanceDesc)
        // 
        // MATCH THIS: Every instance of StaticMesh is treated as an array of geometries (see RTAccelerationStructureBuilder::QueryGeometryDescArray)
        //      thus -> as each StaticMesh consists of array of Submeshes, each Submesh would have a corresponding D3D12_RAYTRACING_GEOMETRY_DESC
        //              representation inside BLAS
        //      thus -> each StaticSubmesh instance would get its own respective GeometryIndex() based on submission order, which is 'consequtive' (no sorting)
        // 
        // MATCH THIS: Respectively, as StaticSubmeshes correspond to 'geometry', Material (see Material.h) instances correspond to 'material' and get their own index as well
        //      materials are not queried by raytracing intrinsics though, but instead should be accessed using materialIndex stored inside GeometryData
        // 
        // Remark: To correspond for missing 'instances', each geometry entry would have a mat4x4 'surfaceToWorld' that can be used to reconstruct data into world space
        // - - - - - - - - - - - - - - - 

        // start pre-processing
        // calculate the amount of static meshes (And thus materials/attributes)
        uint32_t numStaticMeshes = static_cast<uint32_t>(GetStaticMeshes().size());
        for (uint32_t meshIndex = 0; meshIndex < numStaticMeshes; ++meshIndex)
        {
            const StaticMesh& staticMesh = GetStaticMeshes()[meshIndex];

            uint32_t numSubmeshes = static_cast<uint32_t>(staticMesh.Submeshes.size());
            for (uint32_t geometryIndex = 0; geometryIndex < numSubmeshes; ++geometryIndex)
            {
                const StaticSubmesh& submesh = staticMesh.Submeshes.at(geometryIndex);

                StaticMeshGeometryData& geometryData = m_meshGeometries.emplace_back(StaticMeshGeometryData());
                geometryData.surfaceToWorld = staticMesh.InstanceToWorld;
                geometryData.indexBufferIndex = (submesh.IndexBuffer) ? m_bindlessBuffers.AddResource(submesh.IndexBuffer) : PathtracerInvalidBindlessIndex;
                geometryData.indexBufferOffset = submesh.IndicesOffset;
                geometryData.indexBufferStride = submesh.IndicesStride;
                geometryData.indexBufferSizeInbytes = submesh.NumIndices * submesh.IndicesStride;
                geometryData.numVertices = submesh.NumVertices;

                for (uint32_t i = 0; i < eAttributeType_NumTypes; ++i)
                {
                    EAttributeType type = EAttributeType(i);

                    NEB_ASSERT((submesh.AttributeOffsets[type] & 3) == 0, "Attribute offset should be 4-byte aligned");
                    geometryData.attributeBufferIndices[type] = submesh.AttributeBuffers[type] ? m_bindlessBuffers.AddResource(submesh.AttributeBuffers[type]) : PathtracerInvalidBindlessIndex;
                    geometryData.attributeBufferOffsets[type] = submesh.AttributeOffsets[type];
                    geometryData.attributeBufferStrides[type] = submesh.AttributeStrides[type];
                }

                const Material& material = staticMesh.SubmeshMaterials.at(geometryIndex);

                // Get next material index
                uint32_t materialIndex = static_cast<uint32_t>(m_meshMaterials.size());
                geometryData.materialIndex = materialIndex;

                StaticMeshMaterialData& materialData = m_meshMaterials.emplace_back(StaticMeshMaterialData());
                materialData.albedo = material.AlbedoFactor;
                materialData.roughnessMetalness = material.RoughnessMetalnessFactor;

                for (uint32_t i = 0; i < eMaterialTextureType_NumTypes; ++i)
                {
                    EMaterialTextureType type = EMaterialTextureType(i);
                    materialData.textureIndices[type] = (material.Textures[type]) ? m_bindlessTextures.AddResource(material.Textures[type]) : PathtracerInvalidBindlessIndex;
                }
            }
        }

#if 0
        // reconstruct GeometryData to validate RT params
        Neb::WriteBinaryFile("geometry/cpu_geometry_data.bin", std::span(m_meshGeometries));
        //
        // For GeometryData-to-StaticMesh scene validation with Sponza
        for (uint32_t meshIndex = 0; meshIndex < numStaticMeshes; ++meshIndex)
        {
            const StaticMesh& staticMesh = GetStaticMeshes()[meshIndex];
            uint32_t numSubmeshes = static_cast<uint32_t>(staticMesh.Submeshes.size());
            for (uint32_t geometryIndex = 0; geometryIndex < numSubmeshes; ++geometryIndex)
            {
                const StaticSubmesh& submesh = staticMesh.Submeshes.at(geometryIndex);
                Neb::WriteBinaryFile(std::format("geometry/cpu_attr_mesh_{}_geometry_{}_positions.bin", meshIndex, geometryIndex), std::span(submesh.Attributes[eAttributeType_Position]));
                Neb::WriteBinaryFile(std::format("geometry/cpu_attr_mesh_{}_geometry_{}_normals.bin", meshIndex, geometryIndex), std::span(submesh.Attributes[eAttributeType_Normal]));
                Neb::WriteBinaryFile(std::format("geometry/cpu_attr_mesh_{}_geometry_{}_tex_uvs.bin", meshIndex, geometryIndex), std::span(submesh.Attributes[eAttributeType_TexCoords]));
                Neb::WriteBinaryFile(std::format("geometry/cpu_attr_mesh_{}_geometry_{}_tangents.bin", meshIndex, geometryIndex), std::span(submesh.Attributes[eAttributeType_Tangents]));
                NEB_LOG_INFO("For mesh {}, geometry {}: offsets = [{}, {}, {}, {}], sizes = [{}, {}, {}, {}]", meshIndex, geometryIndex,
                    submesh.AttributeOffsets[eAttributeType_Position],
                    submesh.AttributeOffsets[eAttributeType_Normal],
                    submesh.AttributeOffsets[eAttributeType_TexCoords],
                    submesh.AttributeOffsets[eAttributeType_Tangents],
                    submesh.AttributeStrides[eAttributeType_Position] * submesh.NumVertices,
                    submesh.AttributeStrides[eAttributeType_Normal] * submesh.NumVertices,
                    submesh.AttributeStrides[eAttributeType_TexCoords] * submesh.NumVertices,
                    submesh.AttributeStrides[eAttributeType_Tangents] * submesh.NumVertices
                    );
            }
        }
#endif

        if (!createResourceContext)
        {
            NEB_LOG_WARN("GIProcessedScene -> specified not to create resource context... This will skip any GPU upload jobs");
        }
        else
        {
            ThrowIfFalse(CreateResources(), "Failed to create GPU resources for GI scene");
            ThrowIfFalse(CreateDescriptors(), "Failed to create descriptors for GI scene");
            WaitResourcesOnHost();
        }

        return true;
    }

    bool GIProcessedScene::CreateResources()
    {
        NRIDevice& device = NRIDevice::Get();

        if (!m_stagingCommandList)
        {
            NEB_ASSERT(!m_copyFence, "Copy fence was initialized before staging command list?");

            ThrowIfFailed(device.GetD3D12Device()->CreateCommandList1(0,
                D3D12_COMMAND_LIST_TYPE_COPY,
                D3D12_COMMAND_LIST_FLAG_NONE,
                IID_PPV_ARGS(m_stagingCommandList.GetAddressOf())));

            ThrowIfFailed(device.GetD3D12Device()->CreateFence(
                m_copyFenceValue,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(m_copyFence.GetAddressOf())));

            m_copyFenceValue = 0;
        }

        CommandAllocatorPool& allocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Copy);
        D3D12Rc<ID3D12CommandAllocator> allocator = allocatorPool.QueryAllocator();
        ThrowIfFailed(m_stagingCommandList->Reset(allocator.Get(), nullptr));
        {
            ThrowIfFalse(CreateAndUploadResources());
        }
        ThrowIfFailed(m_stagingCommandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_stagingCommandList.Get() };
        ID3D12CommandQueue* copyQueue = device.GetCommandQueue(nri::eCommandContextType_Copy);
        copyQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        ThrowIfFailed(copyQueue->Signal(m_copyFence.Get(), ++m_copyFenceValue));
        allocatorPool.DiscardAllocator(allocator, m_copyFence.Get(), m_copyFenceValue);
        return true;
    }

    void GIProcessedScene::WaitResourcesOnHost()
    {
        NRIDevice& device = NRIDevice::Get();

        // At the very end, when we are done - wait asset processing for completion
        if (m_copyFence->GetCompletedValue() < m_copyFenceValue)
        {
            // Wait until the fence is completed.
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            NEB_ASSERT(fenceEvent != NULL, "Failed to create HANDLE for event");

            ThrowIfFailed(m_copyFence->SetEventOnCompletion(m_copyFenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    template<typename T>
    Rc<ID3D12Resource> CreateResourceAndUpload(ID3D12GraphicsCommandList* commandList, std::span<const T> range, std::string_view resourceName, std::vector<Rc<ID3D12Resource>>& stagingResources)
    {
        NRIDevice& device = NRIDevice::Get();
        
        // Upload geometry data
        {
            NEB_LOG_INFO("GIProcessedScene -> submitting '{}' to GPU", resourceName.data());

            size_t numBytes = range.size_bytes();

            D3D12MA::Allocator* allocator = device.GetResourceAllocator();
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ .SizeInBytes = numBytes, .Alignment = 0 });

            // destination
            Rc<ID3D12Resource> resource;
            {
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
                };

                Rc<D3D12MA::Allocation> allocation;
                ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                                  nullptr, allocation.GetAddressOf(),
                                  IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())),
                    "Failed to create geometry data D3D12 buffer");

                NEB_SET_HANDLE_NAME(resource, "{} ({} bytes)", resourceName.data(), numBytes);
            }

            // upload buffer
            Rc<ID3D12Resource> uploadBuffer;
            {
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_UPLOAD,
                };
                Rc<D3D12MA::Allocation> allocation;
                ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, // This is the required starting state for an upload heap
                                  nullptr, allocation.GetAddressOf(),
                                  IID_PPV_ARGS(uploadBuffer.GetAddressOf())),
                    "Failed to create upload buffer for geometry data");

                NEB_SET_HANDLE_NAME(uploadBuffer, "{} (upload buffer)", resourceName.data());

                stagingResources.push_back(uploadBuffer);

                void* data;
                ThrowIfFailed(uploadBuffer->Map(0, nullptr, &data));
                std::memcpy(data, range.data(), numBytes);
                uploadBuffer->Unmap(0, nullptr);
            }
            commandList->CopyBufferRegion(resource.Get(), 0, uploadBuffer.Get(), 0, numBytes);
            return resource;
        }
    }

    bool GIProcessedScene::CreateAndUploadResources()
    {
        NRIDevice& device = NRIDevice::Get();
        ID3D12GraphicsCommandList* commandList = m_stagingCommandList.Get();

        m_stagingResources.clear();
        m_geometryData = CreateResourceAndUpload(commandList, std::span(m_meshGeometries.cbegin(), m_meshGeometries.cend()), "GeometryData buffer", m_stagingResources);
        m_materialData = CreateResourceAndUpload(commandList, std::span(m_meshMaterials.cbegin(), m_meshMaterials.cend()), "MaterialData buffer", m_stagingResources);
        return true;
    }

    bool GIProcessedScene::CreateDescriptors()
    {
        NRIDevice& device = NRIDevice::Get();
        DescriptorHeap& heap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        {
            // populate bindless buffers
            m_bindlessBufferHeap = heap.AllocateDescriptors(m_bindlessBuffers.GetSize());
            for (uint32_t i = 0; i < m_bindlessBuffers.GetSize(); ++i) // we assume that resources are unique!
            {
                Rc<ID3D12Resource> buffer = m_bindlessBuffers.resources.at(i);
                D3D12_RESOURCE_DESC bufferDesc = buffer->GetDesc();
                D3D12_SHADER_RESOURCE_VIEW_DESC srv = {
                    .Format = DXGI_FORMAT_R32_TYPELESS,
                    .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                    .Buffer = D3D12_BUFFER_SRV{
                        .FirstElement = 0,
                        .NumElements = static_cast<UINT>(bufferDesc.Width / 4u),
                        .StructureByteStride = 0,
                        .Flags = D3D12_BUFFER_SRV_FLAG_RAW,
                    }
                };
                device.GetD3D12Device()->CreateShaderResourceView(buffer.Get(), &srv, m_bindlessBufferHeap.CpuAt(i));
            }

            // populate bindless textures
            m_bindlessTextureHeap = heap.AllocateDescriptors(m_bindlessTextures.GetSize());
            for (uint32_t i = 0; i < m_bindlessTextures.GetSize(); ++i) // we assume that resources are unique!
            {
                static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC NullDescriptorSrvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
                    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                    .Texture2D = D3D12_TEX2D_SRV{ .MipLevels = 1 }
                };

                // https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#null-descriptors
                // passing NULL for the resource pointer in the descriptor definition achieves the effect of an 'unbound' resource.
                Rc<ID3D12Resource> texture = m_bindlessTextures.resources.at(i);
                device.GetD3D12Device()->CreateShaderResourceView(texture.Get(), (texture) ? nullptr : &NullDescriptorSrvDesc, m_bindlessTextureHeap.CpuAt(i));
            }
        }

        // Allocate geometry data descriptors (structured buffers)
        m_meshGeometryDataHeap = heap.AllocateDescriptors(1);
        m_meshMaterialDataHeap = heap.AllocateDescriptors(1);
        {
            ID3D12Resource* geometryData = GetGeometryDataBuffer();            
            D3D12_SHADER_RESOURCE_VIEW_DESC geometrySrvDesc = {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer = D3D12_BUFFER_SRV{
                    .FirstElement = 0,
                    .NumElements = static_cast<uint32_t>(m_meshGeometries.size()),
                    .StructureByteStride = sizeof(StaticMeshGeometryData),
                },
            };
            device.GetD3D12Device()->CreateShaderResourceView(geometryData, &geometrySrvDesc, m_meshGeometryDataHeap.CpuAddress);

            ID3D12Resource* materialData = GetMaterialDataBuffer();
            D3D12_SHADER_RESOURCE_VIEW_DESC materialSrvDesc = {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer = D3D12_BUFFER_SRV{
                    .FirstElement = 0,
                    .NumElements = static_cast<uint32_t>(m_meshMaterials.size()),
                    .StructureByteStride = sizeof(StaticMeshMaterialData),
                },
            };
            device.GetD3D12Device()->CreateShaderResourceView(materialData, &materialSrvDesc, m_meshMaterialDataHeap.CpuAddress);
        }

        return true;
    }

} // Neb::nri namespace