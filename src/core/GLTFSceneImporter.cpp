#include "GLTFSceneImporter.h"

#include <array>
#include "../common/Defines.h"
#include "../common/Log.h"

namespace Neb
{

    bool GLTFSceneImporter::ImportScenesFromFile(const std::filesystem::path& filepath)
    {
        if (!m_nriManager)
            return false;

        Clear(); // cleanup before work
        std::string err, warn;

        if (!m_GLTFLoader.LoadASCIIFromFile(&m_GLTFModel, &err, &warn, filepath.string()))
        {
            NEB_ASSERT(!err.empty());
            NEB_LOG_ERROR("{}", err);
            return false;
        }

        if (!warn.empty())
        {
            NEB_LOG_WARN("{}", warn);
        }

        nri::ThrowIfFalse(SubmitD3D12Resources());

        for (tinygltf::Scene& src : m_GLTFModel.scenes)
        {
            std::unique_ptr<GLTFScene> scene = std::make_unique<GLTFScene>();
            if (ImportScene(scene.get(), src))
            {
                // If successfully imported - move the scene to the list of imported ones,
                // otherwise just discard
                ImportedScenes.push_back(std::move(scene));
            }
        }

        // Before returning wait for scene to be fully loaded
        WaitD3D12Resources();
        m_stagingBuffers.clear(); // cleanup staging buffers
        
        return !ImportedScenes.empty(); // If no scenes were imported then we failed apparently
    }

    void GLTFSceneImporter::Clear()
    {
        // You should not cleanup the importer while it is processing resources
        NEB_ASSERT(m_stagingBuffers.empty());

        ImportedScenes.clear();
        m_GLTFLoader = tinygltf::TinyGLTF(); // just in case clean it up as well
        m_GLTFModel = tinygltf::Model(); // destroy this as well
        m_GLTFTextures.clear();
        m_GLTFBuffers.clear();
    }

    bool GLTFSceneImporter::ImportScene(GLTFScene* scene, tinygltf::Scene& src)
    {
        for (int32_t nodeID : src.nodes)
        {
            if (!ImportGLTFNode(scene, src, nodeID))
            {
                NEB_LOG_ERROR("Failed to import node with nodeID {}", nodeID);
                return false;
            }
        }
        return true;
    }

    bool GLTFSceneImporter::SubmitD3D12Resources()
    {
        NEB_ASSERT(m_nriManager);
        
        // Check if staging command list is created. If not - lazy initialize it
        if (!m_stagingCommandList)
        {
            nri::ThrowIfFailed(m_nriManager->GetDevice()->CreateCommandList1(0, 
                D3D12_COMMAND_LIST_TYPE_COPY, 
                D3D12_COMMAND_LIST_FLAG_NONE, 
                IID_PPV_ARGS(m_stagingCommandList.GetAddressOf())
            ));
        }
        
        NEB_ASSERT(m_stagingCommandList);
        nri::ThrowIfFailed(m_stagingCommandList->Reset(m_nriManager->GetCommandAllocator(nri::eCommandContextType_Copy), nullptr));
        {
            nri::ThrowIfFalse(SubmitD3D12Images());
            nri::ThrowIfFalse(SubmitD3D12Buffers());
        }
        nri::ThrowIfFailed(m_stagingCommandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_stagingCommandList.Get() };
        ID3D12CommandQueue* copyQueue = m_nriManager->GetCommandQueue(nri::eCommandContextType_Copy);
        copyQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        ID3D12Fence* copyFence = m_nriManager->GetFence(nri::eCommandContextType_Copy);
        UINT64& copyFenceValue = m_nriManager->GetFenceValue(nri::eCommandContextType_Copy);
        nri::ThrowIfFailed(copyQueue->Signal(copyFence, ++copyFenceValue));
        return true;
    }

    bool GLTFSceneImporter::SubmitD3D12Images()
    {
        const size_t numImages = m_GLTFModel.images.size();
        m_GLTFTextures.clear();
        m_GLTFTextures.resize(numImages);
        m_stagingBuffers.reserve(m_stagingBuffers.size() + numImages);

        for (size_t i = 0; i < numImages; ++i)
        {
            NEB_ASSERT(m_GLTFTextures[i] == nullptr); // they cannot be valid here
            tinygltf::Image& src = m_GLTFModel.images[i];

            // Information for upload resource
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
            UINT numRows = 0;
            UINT64 numBytesInRow, numTotalBytes;

            // Destination resource
            {
                D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, src.width, src.height, 1, 1);
                D3D12MA::Allocator* allocator = m_nriManager->GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(m_GLTFTextures[i].ReleaseAndGetAddressOf())) // Release just in case despite we assume it is null
                );

                UINT numSubresources = resourceDesc.MipLevels * resourceDesc.DepthOrArraySize;
                m_nriManager->GetDevice()->GetCopyableFootprints(
                    &resourceDesc,
                    0, numSubresources,
                    0,
                    &footprint,
                    &numRows, &numBytesInRow, &numTotalBytes
                );
            }

            // Upload desc
            nri::D3D12Rc<ID3D12Resource> uploadBuffer;
            {
                D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ .SizeInBytes = numTotalBytes, .Alignment = 0 });
                D3D12MA::Allocator* allocator = m_nriManager->GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_UPLOAD,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, // This is the required starting state for an upload heap
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(uploadBuffer.GetAddressOf())) // Release just in case despite we assume it is null
                );
                m_stagingBuffers.push_back(uploadBuffer);

                // Now we want to map staging buffer and copy data into it
                void* mapping = nullptr;
                nri::ThrowIfFailed(m_stagingBuffers[i]->Map(0, nullptr, &mapping));

                // Copy each row into mapping
                uint8_t* data = reinterpret_cast<uint8_t*>(mapping);
                for (UINT i = 0; i < numRows; ++i)
                {
                    uint8_t* copyTo = data + (numBytesInRow * i);
                    uint8_t* copyFrom = src.image.data() + (src.width * src.component * i);
                    const size_t numBytesToCopy = src.width * src.component;
                    std::memcpy(copyTo, copyFrom, numBytesToCopy);
                }
            }

            // Finally, submit copy work from upload buffer to destination texture
            D3D12_TEXTURE_COPY_LOCATION dstLocation = CD3DX12_TEXTURE_COPY_LOCATION(m_GLTFTextures[i].Get(), 0);
            D3D12_TEXTURE_COPY_LOCATION srcLocation = CD3DX12_TEXTURE_COPY_LOCATION(m_stagingBuffers[i].Get(), footprint);
            m_stagingCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        }

        return true;
    }

    bool GLTFSceneImporter::SubmitD3D12Buffers()
    {
        const size_t numBuffers = m_GLTFModel.buffers.size();
        m_GLTFBuffers.clear();
        m_GLTFBuffers.resize(numBuffers);
        m_stagingBuffers.reserve(m_stagingBuffers.size() + numBuffers);

        for (size_t i = 0; i < numBuffers; ++i)
        {
            tinygltf::Buffer& src = m_GLTFModel.buffers[i];

            SIZE_T numBytes = src.data.size();

            // destination
            {
                D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ .SizeInBytes = numBytes, .Alignment = 0 });
                D3D12MA::Allocator* allocator = m_nriManager->GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(m_GLTFBuffers[i].GetAddressOf()))
                );
            }

            nri::D3D12Rc<ID3D12Resource> uploadBuffer;
            {
                D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ .SizeInBytes = numBytes, .Alignment = 0 });
                D3D12MA::Allocator* allocator = m_nriManager->GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_UPLOAD,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ, // This is the required starting state for an upload heap
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(uploadBuffer.GetAddressOf()))
                );
                m_stagingBuffers.push_back(uploadBuffer);

                void* data;
                nri::ThrowIfFailed(uploadBuffer->Map(0, nullptr, &data));
                std::memcpy(data, src.data.data(), numBytes);
            }
        }

        return true;
    }

    void GLTFSceneImporter::WaitD3D12Resources()
    {
        ID3D12Fence* copyFence = m_nriManager->GetFence(nri::eCommandContextType_Copy);
        UINT64& copyFenceValue = m_nriManager->GetFenceValue(nri::eCommandContextType_Copy);

        // At the very end, when we are done - wait asset processing for completion
        if (copyFence->GetCompletedValue() < copyFenceValue)
        {
            // Wait until the fence is completed.
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            NEB_ASSERT(fenceEvent != NULL);

            nri::ThrowIfFailed(copyFence->SetEventOnCompletion(copyFenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    bool GLTFSceneImporter::ImportGLTFNode(GLTFScene* scene, tinygltf::Scene& src, int32_t nodeID)
    {
        NEB_ASSERT(nodeID >= 0);
        tinygltf::Node& node = m_GLTFModel.nodes[nodeID];

        if (node.mesh != -1)
        {
            tinygltf::Mesh& mesh = m_GLTFModel.meshes[node.mesh];
            if (!ImportStaticMesh(scene->StaticMeshes.emplace_back(), mesh))
            {
                NEB_LOG_ERROR("GLTFSceneImporter::ImportScene -> Failed to import static mesh \"{}\"... Returning!", mesh.name);
                return false;
            }
        }
        else
        {
            // If node has no mesh still return true, as we dont want to fail on such nodes
            NEB_LOG_WARN("GLTFSceneImporter::ImportScene -> Skipping node \"{}\" as it does not have mesh component", node.name);
        }

        for (int32_t childID : node.children)
        {
            if (!ImportGLTFNode(scene, src, childID))
                return false;
        }
        return true;
    }

    bool GLTFSceneImporter::ImportStaticMesh(nri::StaticMesh& mesh, tinygltf::Mesh& src)
    {
        // For glTF 2.0 Spec meshes overview chill here - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
        for (tinygltf::Primitive& primitive : src.primitives)
        {
            // Process submesh
            nri::StaticSubmesh& submesh = mesh.Submeshes.emplace_back();

            // For attribute spec there is a table in https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
            // Each attribute has its own name defined by spec which is good
            
            // We also want a few healthy assertions be done before processing submeshes' vertices
            NEB_ASSERT(primitive.attributes.contains("POSITION")); // Nothing to do without positions!
            // Hate to handle those, just assume they are there, we could work it around by generating for primitives though
            NEB_ASSERT(primitive.attributes.contains("NORMAL")); 
            // Nebulae also assumes tex coords are there. Just export glTF with texcoords generated
            // FYI from spec: Client implementations SHOULD support at least two texture coordinate sets, one vertex color, and one joints/weights set.
            // we support only 1 now :(
            NEB_ASSERT(primitive.attributes.contains("TEXCOORD_0"));
            
            static const std::pair<std::string, nri::EAttributeType> AttributeMap[] = {
                { "POSITION", nri::eAttributeType_Position },
                { "NORMAL", nri::eAttributeType_Normal },
                { "TEXCOORD_0", nri::eAttributeType_TexCoords },
            };

            // Set amount of vertices before processing (to get more healthy checks)
            submesh.NumVertices = m_GLTFModel.accessors[primitive.attributes["POSITION"]].count;
    
            // Process each attribute separately
            for (auto& [attribute, type] : AttributeMap)
            {
                tinygltf::Accessor& accessor = m_GLTFModel.accessors[primitive.attributes[attribute]];
                NEB_ASSERT(accessor.count == submesh.NumVertices); // should be equal, otherwise our bad

                // Buffer views are optional in specification of glTF 2.0, but they are required in tinygltf
                // because tinygltf does not support sparse accessors
                tinygltf::BufferView& bufferView = m_GLTFModel.bufferViews[accessor.bufferView];
                tinygltf::Buffer& bytes = m_GLTFModel.buffers[bufferView.buffer];

                // Copy buffer info, we treat it as a byte-stream
                submesh.Attributes[type].clear();
                submesh.Attributes[type].resize(bufferView.byteLength);
                std::memcpy(
                    submesh.Attributes[type].data(), 
                    bytes.data.data() + bufferView.byteOffset + accessor.byteOffset, 
                    bufferView.byteLength - accessor.byteOffset);

                // Store stride of an attribute as well (in bytes)
                const UINT stride = accessor.ByteStride(bufferView);
                NEB_ASSERT(stride != -1); // fail if invalid
                submesh.AttributeStrides[type] = stride;

                nri::D3D12Rc<ID3D12Resource> buffer = m_GLTFBuffers[bufferView.buffer];
                submesh.AttributeBuffers[type] = buffer;
                submesh.AttributeViews[type] = D3D12_VERTEX_BUFFER_VIEW{
                    .BufferLocation = buffer->GetGPUVirtualAddress() + bufferView.byteOffset + accessor.byteOffset,
                    .SizeInBytes = static_cast<UINT>(bufferView.byteLength - accessor.byteOffset),
                    .StrideInBytes = stride,
                };
            }

            // Now process indices
            if (primitive.indices >= 0)
            {
                tinygltf::Accessor& accessor = m_GLTFModel.accessors[primitive.indices];
                tinygltf::BufferView& bufferView = m_GLTFModel.bufferViews[accessor.bufferView];
                tinygltf::Buffer& bytes = m_GLTFModel.buffers[bufferView.buffer];

                // TODO: Check if that makes sense?
                submesh.NumIndices = accessor.count;
                
                // Copy buffer info, we treat it as a byte-stream
                submesh.Indices.clear();
                submesh.Indices.resize(bufferView.byteLength);
                std::memcpy(
                    submesh.Indices.data(),
                    bytes.data.data() + bufferView.byteOffset + accessor.byteOffset,
                    bufferView.byteLength - accessor.byteOffset);

                DXGI_FORMAT format;
                switch (accessor.componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: format = DXGI_FORMAT_R8_UINT; break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: format = DXGI_FORMAT_R16_UINT; break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: format = DXGI_FORMAT_R32_UINT; break;
                default: NEB_ASSERT(false); format = DXGI_FORMAT_UNKNOWN;
                }

                nri::D3D12Rc<ID3D12Resource> buffer = m_GLTFBuffers[bufferView.buffer];
                submesh.IndexBuffer = buffer;
                submesh.IBView = D3D12_INDEX_BUFFER_VIEW{
                    .BufferLocation = buffer->GetGPUVirtualAddress() + bufferView.byteOffset + accessor.byteOffset,
                    .SizeInBytes = static_cast<UINT>(bufferView.byteLength - accessor.byteOffset),
                    .Format = format,
                };
            }
            else
            {
                // No indices - throw warn because we dont really handle it
                NEB_ASSERT(false);
                NEB_LOG_WARN("No indices!");
            }
            
            // TODO: Currently no implementation for tangents, we do not use them yet. Will implement when needed :)
            // 
            // Optional in Nebulae are texture coords and tangents. If no tangents though - Nebulae generates them
            if (primitive.attributes.contains("TANGENT"))
            {
                // we want to generate bitangents as well
            }
            else
            {
                // If no tangents provided - calculate them using vertices of primitives
            }

            // Process submesh material
            nri::Material& material = mesh.SubmeshMaterials.emplace_back();

            if (primitive.material >= 0)
            {
                tinygltf::Material& srcMaterial = m_GLTFModel.materials[primitive.material];
                tinygltf::PbrMetallicRoughness& pbrMaterial = srcMaterial.pbrMetallicRoughness;
                
                // check if has albedo map
                material.AlbedoMap = GetTextureFromGLTFScene(pbrMaterial.baseColorTexture.index);
                if (!material.AlbedoMap)
                {
                    // Use factor
                    material.AlbedoFactor.x = pbrMaterial.baseColorFactor[0];
                    material.AlbedoFactor.y = pbrMaterial.baseColorFactor[1];
                    material.AlbedoFactor.z = pbrMaterial.baseColorFactor[2];
                    material.AlbedoFactor.w = pbrMaterial.baseColorFactor[3];
                }

                material.NormalMap = GetTextureFromGLTFScene(srcMaterial.normalTexture.index);
                NEB_ASSERT(material.NormalMap); // Always require normal map

                material.RoughnessMetalnessMap = GetTextureFromGLTFScene(pbrMaterial.metallicRoughnessTexture.index);
                if (!material.RoughnessMetalnessMap)
                    material.RoughnessMetalnessFactor = Neb::Vec2(pbrMaterial.roughnessFactor, pbrMaterial.metallicFactor);

            }
            else
            {
                NEB_LOG_WARN("Submesh of mesh {} doesnt have material. Skipping material part", src.name);
            }
        }
        return true;
    }

    nri::D3D12Rc<ID3D12Resource> GLTFSceneImporter::GetTextureFromGLTFScene(int32_t index)
    {
        // check if has texture?
        if (index < 0)
            return nullptr;

        tinygltf::Texture& location = m_GLTFModel.textures[index];

        NEB_ASSERT(location.source >= 0); // Not required (???)... I am worried :(
        nri::D3D12Rc<ID3D12Resource> srcTexture = m_GLTFTextures[location.source];
        return srcTexture;
    }

} // Neb namespace