#include "GLTFSceneImporter.h"

#include <array>
#include "../common/Defines.h"
#include "../common/Log.h"

namespace Neb
{

    bool GLTFSceneImporter::ImportScenesFromFile(const std::filesystem::path& filepath)
    {
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
            std::unique_ptr<Scene> scene = std::make_unique<Scene>();
            if (ImportScene(scene.get(), src))
            {
                // If successfully imported - move the scene to the list of imported ones,
                // otherwise just discard
                ImportedScenes.push_back(std::move(scene));
            }
        }

        // Before returning wait for scene to be fully loaded
        WaitD3D12ResourcesOnCopyQueue();
        
        // At the very end submit postprocessing work for static mesh
        // Postprocessing work may vary, but as of now it is just generating GPU buffer to store tangents
        nri::ThrowIfFalse(SubmitPostprocessingD3D12Resources());
        WaitD3D12ResourcesOnCopyQueue();

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

    bool GLTFSceneImporter::ImportScene(Scene* scene, tinygltf::Scene& src)
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
        nri::Manager& nriManager = nri::Manager::Get();

        // Check if staging command list is created. If not - lazy initialize it
        if (!m_stagingCommandList)
        {
            nri::ThrowIfFailed(nriManager.GetDevice()->CreateCommandList1(0,
                D3D12_COMMAND_LIST_TYPE_COPY, 
                D3D12_COMMAND_LIST_FLAG_NONE, 
                IID_PPV_ARGS(m_stagingCommandList.GetAddressOf())
            ));
        }
        
        NEB_ASSERT(m_stagingCommandList);
        nri::ThrowIfFailed(m_stagingCommandList->Reset(nriManager.GetCommandAllocator(nri::eCommandContextType_Copy), nullptr));
        {
            nri::ThrowIfFalse(SubmitD3D12Images());
            nri::ThrowIfFalse(SubmitD3D12Buffers());
        }
        nri::ThrowIfFailed(m_stagingCommandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_stagingCommandList.Get() };
        ID3D12CommandQueue* copyQueue = nriManager.GetCommandQueue(nri::eCommandContextType_Copy);
        copyQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        ID3D12Fence* copyFence = nriManager.GetFence(nri::eCommandContextType_Copy);
        UINT64& copyFenceValue = nriManager.GetFenceValue(nri::eCommandContextType_Copy);
        nri::ThrowIfFailed(copyQueue->Signal(copyFence, ++copyFenceValue));
        return true;
    }

    bool GLTFSceneImporter::SubmitD3D12Images()
    {
        nri::Manager& nriManager = nri::Manager::Get();

        const size_t numImages = m_GLTFModel.images.size();
        m_GLTFTextures.clear();
        m_GLTFTextures.resize(numImages);
        m_stagingBuffers.reserve(m_stagingBuffers.size() + numImages);

        for (size_t i = 0; i < numImages; ++i)
        {
            NEB_ASSERT(m_GLTFTextures[i] == nullptr); // they cannot be valid here
            tinygltf::Image& src = m_GLTFModel.images[i];
            if (src.image.empty())
                continue;

            // Information for upload resource
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
            UINT numRows = 0;
            UINT64 numBytesInRow, numTotalBytes;

            // Destination resource
            {
                D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, src.width, src.height, 1, 1);
                D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(m_GLTFTextures[i].ReleaseAndGetAddressOf())) // Release just in case despite we assume it is null
                );

                UINT numSubresources = resourceDesc.MipLevels * resourceDesc.DepthOrArraySize;
                nriManager.GetDevice()->GetCopyableFootprints(
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
                D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
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
                nri::ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mapping));

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
            D3D12_TEXTURE_COPY_LOCATION srcLocation = CD3DX12_TEXTURE_COPY_LOCATION(uploadBuffer.Get(), footprint);
            m_stagingCommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        }

        return true;
    }

    bool GLTFSceneImporter::SubmitD3D12Buffers()
    {
        nri::Manager& nriManager = nri::Manager::Get();
     
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
                D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
                D3D12MA::ALLOCATION_DESC allocDesc = {
                    .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                    .HeapType = D3D12_HEAP_TYPE_DEFAULT,
                };

                nri::D3D12Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(
                    &allocDesc,
                    &resourceDesc,
                    D3D12_RESOURCE_STATE_COMMON, // No need to use copy dest state. Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
                    nullptr, allocation.GetAddressOf(),
                    IID_PPV_ARGS(m_GLTFBuffers[i].GetAddressOf()))
                );
                
                // hack to convert from string to wstring using <filesystem>. Not sure if that ok to do
                m_GLTFBuffers[i]->SetName(std::filesystem::path(src.name).wstring().c_str());
            }

            nri::D3D12Rc<ID3D12Resource> uploadBuffer;
            {
                D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ .SizeInBytes = numBytes, .Alignment = 0 });
                D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
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

            m_stagingCommandList->CopyBufferRegion(m_GLTFBuffers[i].Get(), 0, uploadBuffer.Get(), 0, numBytes);
        }

        return true;
    }

    bool GLTFSceneImporter::SubmitPostprocessingD3D12Resources()
    {
        // The main goal if this method is to ensure that all of the post-processing work is submitted to the GPU
        //
        // Initial idea here was to handle manually calculated tangents, as we need to submit them into buffers as well
        // And also we need to create buffer views for each submesh
        nri::Manager& nriManager = nri::Manager::Get();
        NEB_ASSERT(m_stagingCommandList); // Do no lazy initialize it here, just assume it is created

        // Firstly try to early return if no postprocessing needed
        if (!IsTangentPostprocessingNeeded())
            return true;

        nri::ThrowIfFailed(m_stagingCommandList->Reset(nriManager.GetCommandAllocator(nri::eCommandContextType_Copy), nullptr));
        {
            nri::ThrowIfFalse(SubmitTangentPostprocessingD3D12Buffer());
        }
        nri::ThrowIfFailed(m_stagingCommandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_stagingCommandList.Get() };
        ID3D12CommandQueue* copyQueue = nriManager.GetCommandQueue(nri::eCommandContextType_Copy);
        copyQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        ID3D12Fence* copyFence = nriManager.GetFence(nri::eCommandContextType_Copy);
        UINT64& copyFenceValue = nriManager.GetFenceValue(nri::eCommandContextType_Copy);
        nri::ThrowIfFailed(copyQueue->Signal(copyFence, ++copyFenceValue));
        return true;
    }

    bool GLTFSceneImporter::IsTangentPostprocessingNeeded()
    {
        for (auto& scene : ImportedScenes)
            for (nri::StaticMesh& mesh : scene->StaticMeshes)
                for (nri::StaticSubmesh& submesh : mesh.Submeshes)
                {
                    const bool hasRawTangents = !submesh.Attributes[nri::eAttributeType_Tangents].empty();
                    if (hasRawTangents && !submesh.AttributeBuffers[nri::eAttributeType_Tangents])
                        return true;
                }

        return false;
    }

    bool GLTFSceneImporter::SubmitTangentPostprocessingD3D12Buffer()
    {
        static constexpr size_t TangentStride = sizeof(Vec4);

        // Firstly we need to calculate the total amount of tangents in the entire scene hiearachy
        uint32_t numTotalVertices = 0;
        for (auto& scene : ImportedScenes)
            for (nri::StaticMesh& mesh : scene->StaticMeshes)
                for (nri::StaticSubmesh& submesh : mesh.Submeshes)
                {
                    // We shoud assume that there are raw tangents everywhere
                    NEB_ASSERT(!submesh.Attributes[nri::eAttributeType_Tangents].empty());
                    NEB_ASSERT(submesh.AttributeStrides[nri::eAttributeType_Tangents] == TangentStride);
                    numTotalVertices += submesh.NumVertices;
                }
    
        NEB_ASSERT(numTotalVertices > 0);
        nri::Manager& nriManager = nri::Manager::Get();

        size_t numTotalBytes = TangentStride * numTotalVertices;
        nri::D3D12Rc<ID3D12Resource> tangentBuffer = m_GLTFBuffers.emplace_back();
        {
            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ 
                .SizeInBytes = numTotalBytes, 
                .Alignment = 0 
            });
            D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
            D3D12MA::ALLOCATION_DESC allocDesc = {
                .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                .HeapType = D3D12_HEAP_TYPE_DEFAULT,
            };

            nri::D3D12Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(
                &allocDesc,
                &resourceDesc,
                D3D12_RESOURCE_STATE_COMMON, // No need to use copy dest state. Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
                nullptr, allocation.GetAddressOf(),
                IID_PPV_ARGS(tangentBuffer.GetAddressOf()))
            );
        }
        
        nri::D3D12Rc<ID3D12Resource> uploadBuffer;
        {
            D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{ 
                .SizeInBytes = numTotalBytes, 
                .Alignment = 0 
            });
            D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
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

            // Map the data
            void* mapping;
            nri::ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mapping));

            // Now the idea here is for each submesh we would get its data and copy it to the offset buffer
            // Afterwards we would create and offset information for that submesh
            // and create a view for that submesh finally
            std::byte* data = reinterpret_cast<std::byte*>(mapping);
            size_t currentOffsetInBytes = 0;
            for (auto& scene : ImportedScenes)
                for (nri::StaticMesh& mesh : scene->StaticMeshes)
                    for (nri::StaticSubmesh& submesh : mesh.Submeshes)
                    {
                        size_t numBytes = submesh.Attributes[nri::eAttributeType_Tangents].size();
                        std::memcpy(data + currentOffsetInBytes, submesh.Attributes[nri::eAttributeType_Tangents].data(), numBytes);
                        
                        submesh.AttributeBuffers[nri::eAttributeType_Tangents] = tangentBuffer;
                        submesh.AttributeViews[nri::eAttributeType_Tangents] = D3D12_VERTEX_BUFFER_VIEW{
                            .BufferLocation = tangentBuffer->GetGPUVirtualAddress() + currentOffsetInBytes,
                            .SizeInBytes = static_cast<UINT>(numBytes),
                            .StrideInBytes = static_cast<UINT>(TangentStride),
                        };

                        currentOffsetInBytes += numBytes;
                    }
        }

        m_stagingCommandList->CopyBufferRegion(tangentBuffer.Get(), 0, uploadBuffer.Get(), 0, numTotalBytes);
        return true;
    }

    void GLTFSceneImporter::WaitD3D12ResourcesOnCopyQueue()
    {
        nri::Manager& nriManager = nri::Manager::Get();

        ID3D12Fence* copyFence = nriManager.GetFence(nri::eCommandContextType_Copy);
        UINT64& copyFenceValue = nriManager.GetFenceValue(nri::eCommandContextType_Copy);

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

    bool GLTFSceneImporter::ImportGLTFNode(Scene* scene, tinygltf::Scene& src, int32_t nodeID)
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
                { "TANGENT", nri::eAttributeType_Tangents }
            };

            // Set amount of vertices before processing (to get more healthy checks)
            submesh.NumVertices = m_GLTFModel.accessors[primitive.attributes["POSITION"]].count;
    
            // Process each attribute separately
            for (auto& [attribute, type] : AttributeMap)
            {
                // If no such primitive - just skip it
                if (!primitive.attributes.contains(attribute))
                    continue;

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
                submesh.IndicesStride = accessor.ByteStride(bufferView);
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
            
            // TANGENT GENERATION
            // 
            // Optional in Nebulae are texture coords and tangents. If no tangents though - Nebulae generates them
            // In Nebulae to check if attribute is there we could just check its stride and compare it to the stride we need
            if (submesh.Attributes[nri::eAttributeType_Tangents].empty())
            {
                // stride of tangents != 0 -> means there are tangents inside. We can work with them
                // If no tangents provided - calculate them using vertices of primitives
                NEB_ASSERT(submesh.AttributeStrides[nri::eAttributeType_Normal] == sizeof(Vec3));
                NEB_ASSERT(submesh.AttributeStrides[nri::eAttributeType_Position] == sizeof(Vec3));
                NEB_ASSERT(submesh.AttributeStrides[nri::eAttributeType_TexCoords] == sizeof(Vec2));

                // Nebulae just works with triangular static meshes when generating tangents
                // we need that to guarantee that each 3 indices of a primitive will represent a single triangle
                NEB_ASSERT(primitive.mode == TINYGLTF_MODE_TRIANGLES);
                NEB_ASSERT(submesh.NumIndices > 0 && submesh.NumIndices % 3 == 0);
                
                Vec3* normals = reinterpret_cast<Vec3*>(submesh.Attributes[nri::eAttributeType_Normal].data());
                Vec3* positions = reinterpret_cast<Vec3*>(submesh.Attributes[nri::eAttributeType_Position].data());
                Vec2* texCoords = reinterpret_cast<Vec2*>(submesh.Attributes[nri::eAttributeType_TexCoords].data());

                // Resize the target array of tangents
                submesh.AttributeStrides[nri::eAttributeType_Tangents] = sizeof(Vec4);
                submesh.Attributes[nri::eAttributeType_Tangents].resize(sizeof(Vec4) * submesh.NumVertices);
                Vec4* tangents = reinterpret_cast<Vec4*>(submesh.Attributes[nri::eAttributeType_Tangents].data());

                // Needed for tangent and hardedness calculation
                std::vector<Vec3> tan1(submesh.NumVertices);
                std::vector<Vec3> tan2(submesh.NumVertices);

                uint32_t numPrimitives = submesh.NumIndices / 3;
                for (uint32_t i = 0; i < numPrimitives; ++i)
                {
                    // we handle both 32bit and 16bit indices here
                    // https://gamedev.stackexchange.com/questions/68612/how-to-compute-tangent-and-bitangent-vectors
                    uint32_t i0, i1, i2;
                    if (submesh.IndicesStride == sizeof(uint32_t))
                    {
                        uint32_t* indices = reinterpret_cast<uint32_t*>(submesh.Indices.data());
                        i0 = indices[(i * 3) + 0];
                        i1 = indices[(i * 3) + 1];
                        i2 = indices[(i * 3) + 2];
                    }
                    else
                    {
                        NEB_ASSERT(submesh.IndicesStride == sizeof(uint16_t));
                        uint16_t* indices = reinterpret_cast<uint16_t*>(submesh.Indices.data());
                        i0 = indices[(i * 3) + 0];
                        i1 = indices[(i * 3) + 1];
                        i2 = indices[(i * 3) + 2];
                    }

                    const Vec3& pos0 = positions[i0];
                    const Vec3& pos1 = positions[i1];
                    const Vec3& pos2 = positions[i2];
                    Vec3 deltaPos1 = pos1 - pos0;
                    Vec3 deltaPos2 = pos2 - pos0;

                    const Vec2& uv0 = texCoords[i0];
                    const Vec2& uv1 = texCoords[i1];
                    const Vec2& uv2 = texCoords[i2];
                    Vec2 deltaUv1 = uv1 - uv0;
                    Vec2 deltaUv2 = uv2 - uv0;

                    float r = 1.0f / (deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x);
                    Vec3 sd = (deltaPos1 * deltaUv2.y - deltaPos2 * deltaUv1.y) * r;
                    Vec3 td = (deltaPos2 * deltaUv1.x - deltaPos1 * deltaUv2.x) * r;

                    // TODO: From glTF 2.0 spec https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#geometry-overview
                    // Tangent is stored in Vec4, where Vec4.w is a sign value indicating handedness of the tangent basis
                    // We can use that information to calculate bitangent efficiently
                    // We will manually convert signed tangents from glTF to vec3 tangents/bitangents
                    tan1[i0] += sd; tan2[i0] += td;
                    tan1[i1] += sd; tan2[i1] += td;
                    tan1[i2] += sd; tan2[i2] += td;
                }

                for (uint32_t i = 0; i < submesh.NumVertices; ++i)
                {
                    const Vec3& n = normals[i];
                    const Vec3& t = tan1[i];

                    // Gram-Schmidt orthogonalize
                    Vec3 tangent;
                    tangent = (t - n * n.Dot(t));
                    tangent.Normalize();

                    tangents[i].x = tangent.x;
                    tangents[i].y = tangent.y;
                    tangents[i].z = tangent.z;

                    // calculate hardedness
                    tangents[i].w = (n.Cross(t).Dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;
                }
            }

            // Process submesh material
            nri::Material& material = mesh.SubmeshMaterials.emplace_back();

            if (primitive.material >= 0)
            {
                tinygltf::Material& srcMaterial = m_GLTFModel.materials[primitive.material];
                tinygltf::PbrMetallicRoughness& pbrMaterial = srcMaterial.pbrMetallicRoughness;
                
                // check if has albedo map
                material.Textures[nri::eMaterialTextureType_Albedo] = GetTextureFromGLTFScene(pbrMaterial.baseColorTexture.index);
                if (!material.Textures[nri::eMaterialTextureType_Albedo])
                {
                    // Use factor
                    material.AlbedoFactor.x = pbrMaterial.baseColorFactor[0];
                    material.AlbedoFactor.y = pbrMaterial.baseColorFactor[1];
                    material.AlbedoFactor.z = pbrMaterial.baseColorFactor[2];
                    material.AlbedoFactor.w = pbrMaterial.baseColorFactor[3];
                }

                material.Textures[nri::eMaterialTextureType_Normal] = GetTextureFromGLTFScene(srcMaterial.normalTexture.index);
                NEB_ASSERT(material.Textures[nri::eMaterialTextureType_Normal]); // Always require normal map

                material.Textures[nri::eMaterialTextureType_RoughnessMetalness] = GetTextureFromGLTFScene(pbrMaterial.metallicRoughnessTexture.index);
                if (!material.Textures[nri::eMaterialTextureType_RoughnessMetalness])
                    material.RoughnessMetalnessFactor = Neb::Vec2(pbrMaterial.roughnessFactor, pbrMaterial.metallicFactor);

                nri::Manager& nriManager = nri::Manager::Get();
                nri::DescriptorHeap& descriptorHeap = nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                material.SrvRange = descriptorHeap.AllocateDescriptorRange(nri::eMaterialTextureType_NumTypes);
                for (UINT i = 0; i < nri::eMaterialTextureType_NumTypes; ++i)
                    InitMaterialTextureDescriptor(material.Textures[i].Get(), (nri::EMaterialTextureType)i, material.SrvRange);
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

    void GLTFSceneImporter::InitMaterialTextureDescriptor(ID3D12Resource* resource, nri::EMaterialTextureType type, const nri::DescriptorRange& range)
    {
        static constexpr D3D12_SHADER_RESOURCE_VIEW_DESC NullDescriptorSrvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
            .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{ .MipLevels = 1 }
        };

        D3D12_CPU_DESCRIPTOR_HANDLE handle = range.GetCPUHandle(type);
        // https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#null-descriptors
        // passing NULL for the resource pointer in the descriptor definition achieves the effect of an “unbound” resource.
        nri::Manager& nriManager = nri::Manager::Get();
        nriManager.GetDevice()->CreateShaderResourceView(resource, (resource) ? nullptr : &NullDescriptorSrvDesc, handle);
    }

} // Neb namespace