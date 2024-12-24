#include "Raytracer.h"

#include "Nebulae.h" // TODO: Remove

#include "common/Assert.h"
#include "common/Log.h"
#include "core/Math.h"
#include "nri/imgui/UiContext.h"
#include "nri/Device.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/StaticMesh.h"

#include "DXRHelper/nv_helpers_dx12/BottomLevelASGenerator.h"
#include "DXRHelper/nv_helpers_dx12/TopLevelASGenerator.h"
#include "DXRHelper/nv_helpers_dx12/RaytracingPipelineGenerator.h"

// Helper to compute aligned buffer sizes
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))

#include <array>

namespace Neb
{
    BOOL RtScene::InitForScene(nri::Swapchain* swapchain, Scene* scene)
    {
        NEB_ASSERT(scene, "Scene should be a valid pointer");
        m_scene = scene;
        m_swapchain = swapchain;

        // Very first step is to check for device compatibility
        if (nri::NRIDevice::Get().GetCapabilities().RaytracingSupportTier == nri::ESupportTier_Raytracing::NotSupported)
        {
            NEB_LOG_ERROR("Ray tracing is not supported on this device!");
            return false;
        }

        InitCommandList();
        InitASFences();

// #define BETTER_SCENE_SUPPORT
#if defined(BETTER_SCENE_SUPPORT)
        for (const nri::StaticMesh& mesh : m_scene->StaticMeshes)
            AddStaticMesh(mesh);
#else
        NEB_ASSERT(m_scene->StaticMeshes.size() == 1, "No support for more than 1 static mesh currently!");
        AddStaticMesh(m_scene->StaticMeshes.front());
#endif // defined(BETTER_SCENE_SUPPORT)

        nri::ThrowIfFalse(InitResourcesAndDescriptors(m_swapchain->GetWidth(), m_swapchain->GetHeight()), "Failed to initialize resources or descriptors");

        nri::ThrowIfFalse(InitRaytracingPipeline(), "Failed to initialize ray tracing pipeline");
        nri::ThrowIfFalse(InitShaderBindingTable(), "Failed to initialize SBT");

        return true;
    }

    void RtScene::WaitForGpuContext()
    {
        WaitForFenceCompletion();
    }

    void RtScene::Resize(UINT width, UINT height)
    {
        nri::ThrowIfFalse(InitResourcesAndDescriptors(width, height), "Failed to initialize resources or descriptors");
    }

    void RtScene::PopulateCommandLists(UINT frameIndex)
    {
        NEB_ASSERT(m_scene && m_swapchain, "Scene and swapchain should be valid for command list population");
        nri::NRIDevice& device = nri::NRIDevice::Get();

        if (ImGui::Button("Reload shaders"))
        {
            if (this->InitRaytracingPipeline())
            {
                nri::ThrowIfFalse(this->InitShaderBindingTable());
            }
        }

        std::array heaps = { device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap() };
        m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

        ID3D12Resource* backbuffer = m_swapchain->GetCurrentBackbuffer();
        ID3D12Resource* outputBuffer = m_outputBuffer.Get();


        // TODO: This code is duplicated from renderer.cpp, should be somehow removed
        {
            const Mat4& view = m_scene->Camera.UpdateLookAt();
            const float aspectRatio = m_swapchain->GetWidth() / static_cast<float>(m_swapchain->GetHeight());
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);
            Mat4 viewProjInverse = Mat4(view * projection).Invert();

            const Vec3 eye = m_scene->Camera.GetEyePos();

            RtInstanceInfoCb cbInstanceInfo = RtInstanceInfoCb{
                .ViewProjInverse = viewProjInverse,
                .CameraWorldPos = Vec4(eye.x, eye.y, eye.z, 1.0f),
            };

            std::memcpy(m_cbInstanceInfo.GetMapping<RtInstanceInfoCb>(frameIndex), &cbInstanceInfo, sizeof(RtInstanceInfoCb));
        }

        m_commandList->SetComputeRootSignature(m_rtGlobalRS.GetD3D12RootSignature());
        m_commandList->SetComputeRootConstantBufferView(0, m_cbInstanceInfo.GetGpuVirtualAddress(frameIndex));
        m_commandList->SetComputeRootDescriptorTable(1, m_rtDescriptors.GpuAt(eDescriptorSlot_TlasSrv));

        // todo: figure this stuff out

        // Setup the raytracing task
        D3D12_DISPATCH_RAYS_DESC desc = {};
        // The layout of the SBT is as follows: ray generation shader, miss
        // shaders, hit groups. As described in the CreateShaderBindingTable method,
        // all SBT entries of a given type have the same size to allow a fixed stride.

        // The ray generation shaders are always at the beginning of the SBT.
        // important to do in order to align with currentTableOffset
        uint32_t currentTableOffset = 0;
        D3D12_GPU_VIRTUAL_ADDRESS sbtAddress = m_sbtBuffer->GetGPUVirtualAddress();
        desc.RayGenerationShaderRecord.StartAddress = sbtAddress;
        desc.RayGenerationShaderRecord.SizeInBytes = m_sbtGenerator.GetRayGenSectionSize();
        currentTableOffset += desc.RayGenerationShaderRecord.SizeInBytes;

        desc.MissShaderTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        desc.MissShaderTable.StrideInBytes = m_sbtGenerator.GetMissEntrySize();
        desc.MissShaderTable.SizeInBytes = m_sbtGenerator.GetMissSectionSize();
        currentTableOffset += desc.MissShaderTable.SizeInBytes;

        desc.HitGroupTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        desc.HitGroupTable.StrideInBytes = m_sbtGenerator.GetHitGroupEntrySize();
        desc.HitGroupTable.SizeInBytes = m_sbtGenerator.GetHitGroupSectionSize();
        currentTableOffset += desc.HitGroupTable.SizeInBytes;

        desc.Width = m_swapchain->GetWidth();
        desc.Height = m_swapchain->GetHeight();
        desc.Depth = 1;

        m_commandList->SetPipelineState1(m_rtPso.Get());
        m_commandList->DispatchRays(&desc);

        // transition output buffer back into copy source
        // copy output resources to swappchain as needed
        {
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(outputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            m_commandList->CopyResource(backbuffer, outputBuffer);
        }

        // transition resources back as needed
        {
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(outputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
            };
            m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
    }

    void RtScene::AddStaticMesh(const nri::StaticMesh& staticMesh)
    {
        if (staticMesh.Submeshes.empty())
        {
            NEB_LOG_WARN("Tried adding empty static mesh to RtScene");
            return;
        }

        nri::ThrowIfFalse(InitAccelerationStructure(staticMesh));
    }

    void RtScene::InitCommandList()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        m_commandList.Reset();
        nri::D3D12Rc<ID3D12GraphicsCommandList> commandList;
        {
            nri::ThrowIfFailed(device.GetDevice()->CreateCommandList1(0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE,
                IID_PPV_ARGS(commandList.GetAddressOf())));
        }
        nri::ThrowIfFailed(commandList.As(&m_commandList));
    }

    void RtScene::InitASFences()
    {
        m_fenceValue = 0;

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetDevice()->CreateFence(
            m_fenceValue,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(m_ASFence.ReleaseAndGetAddressOf())));
    }

    void RtScene::WaitForFenceCompletion()
    {
        UINT64 fenceValue = m_fenceValue;
        if (m_ASFence->GetCompletedValue() < fenceValue)
        {
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            NEB_ASSERT(fenceEvent, "Failed to create HANDLE for event");

            // Wait until the fence is completed.
            nri::ThrowIfFailed(m_ASFence->SetEventOnCompletion(fenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    RtAccelerationStructureBuffers RtScene::CreateBLAS(std::span<const RtBLASGeometryBuffer> geometryBuffers)
    {
        nv_helpers_dx12::BottomLevelASGenerator blasGenerator;

        for (const RtBLASGeometryBuffer& geometry : geometryBuffers)
        {
            NEB_ASSERT(geometry.VertexStride == sizeof(Vec3), "Only support for vertex stride of {}", sizeof(Vec3));
            NEB_ASSERT(geometry.IndexStride == sizeof(uint16_t) || geometry.IndexStride == sizeof(uint32_t), "Incorrect index stride, should be either UINT16 or UINT32");
            DXGI_FORMAT indexFormat = (geometry.IndexStride == sizeof(uint16_t)) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            blasGenerator.AddVertexBuffer(
                geometry.PositionBuffer.Get(), geometry.VertexOffsetInBytes, geometry.NumVertices, geometry.VertexStride,
                geometry.IndexBuffer.Get(), geometry.IndexOffsetInBytes, geometry.NumIndices, indexFormat,
                nullptr, 0, // we are not using transform buffers in BLAS
                true);
        }

        nri::NRIDevice& device = nri::NRIDevice::Get();

        // we do not update geometry (only static meshes)
        UINT64 numScratchBytes;
        UINT64 numBLASBytes;
        blasGenerator.ComputeASBufferSizes(device.GetDevice(), false, &numScratchBytes, &numBLASBytes);

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT
        };

        RtAccelerationStructureBuffers result;

        // Create scratch buffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
            // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.ScratchBuffer.GetAddressOf())));
        }

        // Create ASBuffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numBLASBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.ASBuffer.GetAddressOf())));
        }

        blasGenerator.Generate(m_commandList.Get(), result.ScratchBuffer.Get(), result.ASBuffer.Get());
        return result;
    }

    RtAccelerationStructureBuffers RtScene::CreateTLAS(std::span<const RtTLASInstanceBuffer> instanceBuffers)
    {
        nv_helpers_dx12::TopLevelASGenerator tlasGenerator;
        for (UINT i = 0; i < instanceBuffers.size(); ++i)
        {
            const RtTLASInstanceBuffer& instance = instanceBuffers[i];
            tlasGenerator.AddInstance(instance.ASBuffer.Get(), instance.Transformation, i, 0);
        }

        nri::NRIDevice& device = nri::NRIDevice::Get();

        UINT64 numScratchBytes;
        UINT64 numTLASBytes;
        UINT64 numInstanceDescriptorBytes;
        tlasGenerator.ComputeASBufferSizes(device.GetDevice(), false, &numScratchBytes, &numTLASBytes, &numInstanceDescriptorBytes);

        RtAccelerationStructureBuffers result;

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_DEFAULT };

        // Create scratch buffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
            // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.ScratchBuffer.GetAddressOf())));
        }

        // Create ASBuffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numTLASBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.ASBuffer.GetAddressOf())));
        }

        // Instance descriptor buffer
        {
            D3D12MA::ALLOCATION_DESC instanceAllocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numInstanceDescriptorBytes, D3D12_RESOURCE_FLAG_NONE);

            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&instanceAllocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(result.InstanceDescriptorBuffer.GetAddressOf())));
        }

        tlasGenerator.Generate(m_commandList.Get(),
            result.ScratchBuffer.Get(),
            result.ASBuffer.Get(),
            result.InstanceDescriptorBuffer.Get(), false, nullptr,
            D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE);
        return result;
    }

    BOOL RtScene::InitAccelerationStructure(const nri::StaticMesh& staticMesh)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);

        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();
        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator.Get(), nullptr));

        RtAccelerationStructureBuffers blas, tlas;
        {
            // Create BLAS
            std::vector<RtBLASGeometryBuffer> geometryBuffers;
            geometryBuffers.reserve(staticMesh.Submeshes.size());

            for (const nri::StaticSubmesh& submesh : staticMesh.Submeshes)
            {
                geometryBuffers.push_back(RtBLASGeometryBuffer{
                    // Position buffer data
                    .PositionBuffer = submesh.AttributeBuffers[nri::eAttributeType_Position],
                    .VertexStride = submesh.AttributeStrides[nri::eAttributeType_Position],
                    .VertexOffsetInBytes = submesh.AttributeOffsets[nri::eAttributeType_Position],
                    .NumVertices = submesh.NumVertices,
                    // Index buffer data (required for Nebulae)
                    .IndexBuffer = submesh.IndexBuffer,
                    .IndexStride = submesh.IndicesStride,
                    .IndexOffsetInBytes = submesh.IndicesOffset,
                    .NumIndices = submesh.NumIndices });
            }
            blas = CreateBLAS(geometryBuffers);

            std::vector<Mat4> instanceTransformations;
            {
                // Was just copied from renderer.cpp => should be handled better honestly, stored in instance data and passed through scene interface
                static const float rotationAngleY = 0.0f;
                static const Vec3 rotationAngles = Vec3(ToRadians(90.0f), ToRadians(rotationAngleY), ToRadians(0.0f));

                Mat4 translation = Mat4::CreateTranslation(Vec3(0.0f, 0.0f, 0.0f));
                Mat4 rotation = Mat4::CreateFromYawPitchRoll(rotationAngles);
                Mat4 scale = Mat4::CreateScale(Vec3(1.0f));

                // TODO: expects row-major, providing column-major. I dont get it (DirectXMath operates in row-major)
                Mat4 instanceToWorld = Mat4(scale * rotation * translation);
                instanceTransformations.push_back(instanceToWorld.Transpose());

                translation = Mat4::CreateTranslation(Vec3(-2.0f, 0.0f, 0.0f));
                instanceToWorld = Mat4(scale * rotation * translation);
                instanceTransformations.push_back(instanceToWorld.Transpose());

                translation = Mat4::CreateTranslation(Vec3(2.0f, 0.0f, 0.0f));
                instanceToWorld = Mat4(scale * rotation * translation);
                instanceTransformations.push_back(instanceToWorld.Transpose());

                translation = Mat4::CreateTranslation(Vec3(0.0f, 0.0f, 2.0f));
                instanceToWorld = Mat4(scale * rotation * translation);
                instanceTransformations.push_back(instanceToWorld.Transpose());
            }

            std::vector<RtTLASInstanceBuffer> instanceBuffers;
            instanceBuffers.reserve(instanceTransformations.size()); // N instances

            for (size_t instanceID = 0; instanceID < instanceTransformations.size(); ++instanceID)
            {
                instanceBuffers.push_back(RtTLASInstanceBuffer{
                    .ASBuffer = blas.ASBuffer,
                    .Transformation = instanceTransformations[instanceID],
                });
            }
            tlas = CreateTLAS(instanceBuffers);
        }
        nri::ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* pCommandList = m_commandList.Get();
        ID3D12CommandQueue* queue = device.GetCommandQueue(nri::eCommandContextType_Graphics);
        UINT fenceValue = ++m_fenceValue;

        // This stuff breaks -> idk why but buffers seem to be fine
        queue->ExecuteCommandLists(1, &pCommandList);
        queue->Signal(m_ASFence.Get(), fenceValue);
        {
            WaitForFenceCompletion();
        }
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_ASFence.Get(), fenceValue);

        // Member assignments
        m_blas = std::move(blas);
        m_tlas = std::move(tlas);
        return true;
    }

    BOOL RtScene::InitRaytracingPipeline()
    {
        // Shader and root-signature related stuff
        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderFilepath = shaderDirectory / "BasicRt.hlsl";

        if (!this->InitRayGen(shaderFilepath) ||
            !this->InitRayClosestHit(shaderFilepath) || 
            !this->InitRayMiss(shaderFilepath))
        {
            NEB_LOG_ERROR("Failed to init raytracing shaders");
            return false;
        }

        nri::NRIDevice& device = nri::NRIDevice::Get();

        // TODO: RayTracingPipelineGenerator leaks memory (probably no cleanup for glocal/local root signature)
        //      fix that either in their src or when moving away from Nvidia's helpers
        nv_helpers_dx12::RayTracingPipelineGenerator pipelineGenerator(device.GetDevice());
        pipelineGenerator.AddLibrary(m_rayGen.GetDxcBinaryBlob(), { L"RayGen" });
        pipelineGenerator.AddLibrary(m_rayMiss.GetDxcBinaryBlob(), { L"Miss" });
        pipelineGenerator.AddLibrary(m_rayClosestHit.GetDxcBinaryBlob(), { L"ClosestHit" });

        pipelineGenerator.AddHitGroup(L"HitGroup", L"ClosestHit");

        pipelineGenerator.AddRootSignatureAssociation(m_rayGenRS.GetD3D12RootSignature(), { L"RayGen" });
        pipelineGenerator.AddRootSignatureAssociation(m_rayMissRS.GetD3D12RootSignature(), { L"Miss" });
        pipelineGenerator.AddRootSignatureAssociation(m_rayClosestHitRS.GetD3D12RootSignature(), { L"HitGroup" });

        pipelineGenerator.SetMaxPayloadSize(4 * sizeof(Vec4));   // see HitInfo struct in BasicRt.hlsl
        pipelineGenerator.SetMaxAttributeSize(2 * sizeof(Vec2)); // see Attributes struct in BasicRt.hlsl
        pipelineGenerator.SetMaxRecursionDepth(1);

        D3D12_DESCRIPTOR_RANGE1 tlasSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        m_rtGlobalRS = nri::RootSignature(2)
                           .AddParamCbv(0, 0)
                           .AddParamDescriptorTable(1, std::array{ tlasSrv });

        nri::ThrowIfFalse(m_rtGlobalRS.Init(&device), "failed to init global rs for rt scene");

        m_rtPso = pipelineGenerator.Generate(m_rtGlobalRS.GetD3D12RootSignature());
        nri::ThrowIfFalse(m_rtPso != nullptr);
        nri::ThrowIfFailed(m_rtPso->QueryInterface(m_rtPsoProperties.ReleaseAndGetAddressOf()));
        return true;
    }

    BOOL RtScene::InitRayGen(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayGen = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("RayGen", shaderModel, nri::EShaderType::RayGen));


        D3D12_DESCRIPTOR_RANGE1 outputTextureUav = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        m_rayGenRS = nri::RootSignature(eRaygenRoot_NumRoots)
                         .AddParamDescriptorTable(eRaygenRoot_OutputUav, std::array{ outputTextureUav });

        m_rayGenRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        return m_rayGen.HasBinary() && m_rayGenRS.IsValid();
    }

    BOOL RtScene::InitRayClosestHit(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayClosestHit = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("ClosestHit", shaderModel, nri::EShaderType::RayClosestHit));

        m_rayClosestHitRS = nri::RootSignature();
        m_rayClosestHitRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        return m_rayClosestHit.HasBinary() && m_rayClosestHitRS.IsValid();
    }

    BOOL RtScene::InitRayMiss(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayMiss = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("Miss", shaderModel, nri::EShaderType::RayMiss));

        m_rayMissRS = nri::RootSignature();
        m_rayMissRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        return m_rayMiss.HasBinary() && m_rayMissRS.IsValid();
    }

    BOOL RtScene::InitResourcesAndDescriptors(UINT width, UINT height)
    {
        if (!InitResources(width, height))
        {
            NEB_LOG_ERROR("Failed to initialize ray tracing resources");
            return false;
        }

        nri::NRIDevice& device = nri::NRIDevice::Get();
        m_rtDescriptors = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(eDescriptorSlot_NumSlots);

        // Allocate UAV for output buffer
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D = D3D12_TEX2D_UAV{
            .MipSlice = 0,
            .PlaneSlice = 0,
        };
        device.GetDevice()->CreateUnorderedAccessView(m_outputBuffer.Get(), nullptr, &uavDesc, m_rtDescriptors.CpuAt(eDescriptorSlot_OutputBufferUav));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_SRV{
            .Location = m_tlas.ASBuffer->GetGPUVirtualAddress()
        };
        device.GetDevice()->CreateShaderResourceView(nullptr, &srvDesc, m_rtDescriptors.CpuAt(eDescriptorSlot_TlasSrv));

        InitInstanceInfoCb();

        return true;
    }

    BOOL RtScene::InitResources(UINT width, UINT height)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT
        };
        D3D12_RESOURCE_DESC outputBufferDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_swapchain->GetFormat(), width, height, 1, 1);
        outputBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // This is UAV, remember?

        nri::Rc<D3D12MA::Allocation> alloc;
        nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &outputBufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               nullptr, alloc.GetAddressOf(),
                               IID_PPV_ARGS(m_outputBuffer.ReleaseAndGetAddressOf())),
            "Failed to create output buffer for ray tracing with {}x{} dimensions", width, height);

        NEB_SET_HANDLE_NAME(m_outputBuffer, "RT Output buffer");
        return true;
    }

    void RtScene::InitInstanceInfoCb()
    {
        nri::ThrowIfFalse(m_cbInstanceInfo.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(RtInstanceInfoCb) }));
    }

    BOOL RtScene::InitShaderBindingTable()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        // tlas goes right after output buffer in the heap
        void* pRawDescriptor = reinterpret_cast<void*>(m_rtDescriptors.GpuAt(eDescriptorSlot_OutputBufferUav).ptr);

        m_sbtGenerator.Reset();
        m_sbtGenerator.AddRayGenerationProgram(L"RayGen", { pRawDescriptor });
        m_sbtGenerator.AddMissProgram(L"Miss", {});
        m_sbtGenerator.AddHitGroup(L"HitGroup", {});
        const uint32_t sbtSize = m_sbtGenerator.ComputeSBTSize();

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };

        // Create shader binding table buffer
        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sbtSize);

            // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
            // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                allocation.GetAddressOf(),
                IID_PPV_ARGS(m_sbtBuffer.ReleaseAndGetAddressOf())));
        }

        NEB_ASSERT(m_sbtBuffer, "Failed to create shader binding table buffer!");

        // Compile the SBT from the shader and parameters info
        try
        {
            m_sbtGenerator.Generate(m_sbtBuffer.Get(), m_rtPsoProperties.Get());
        }
        catch (std::logic_error& error)
        {
            NEB_LOG_ERROR("Failed to generate Shader Binding Table: {}", error.what());
            return FALSE;
        }

        return TRUE;
    }

}