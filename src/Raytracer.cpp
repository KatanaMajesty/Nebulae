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

    BOOL RtScene::Init(nri::Swapchain* swapchain)
    {
        NEB_ASSERT(swapchain, "Swapchain must be valid for RT context to work");
        m_swapchain = swapchain;

        NEB_ASSERT(nri::NRIDevice::Get().GetCapabilities().RaytracingSupportTier != nri::ESupportTier_Raytracing::NotSupported,
            "Hardware raytracing should be supported by selected device");

        return true;
    }

    BOOL RtScene::InitSceneContext(ID3D12GraphicsCommandList4* commandList, Scene* scene)
    {
        NEB_ASSERT(scene, "Scene should be a valid pointer");
        m_scene = scene;

        NEB_ASSERT(m_scene->StaticMeshes.size() == 1, "No support for more than 1 static mesh currently!");
        InitStaticMesh(commandList, m_scene->StaticMeshes.front());

        return true;
    }

    void RtScene::InitStaticMesh(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh)
    {
        if (staticMesh.Submeshes.empty())
        {
            NEB_LOG_WARN("Tried adding empty static mesh to RtScene");
            return;
        }

        nri::ThrowIfFalse(InitAccelerationStructure(commandList, staticMesh));
        nri::ThrowIfFalse(InitResourcesAndDescriptors(m_swapchain->GetWidth(), m_swapchain->GetHeight()), "Failed to initialize resources or descriptors");

        nri::ThrowIfFalse(InitRaytracingPipeline(), "Failed to initialize ray tracing pipeline");
        nri::ThrowIfFalse(InitShaderBindingTable(), "Failed to initialize SBT");
    }

    void RtScene::Resize(UINT width, UINT height)
    {
        nri::ThrowIfFalse(InitResourcesAndDescriptors(width, height), "Failed to initialize resources or descriptors");
    }

    void RtScene::PopulateCommandLists(ID3D12GraphicsCommandList4* commandList, UINT frameIndex, float timestep)
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

        nri::ThrowIfFalse(this->UpdateAccelerationStructure(commandList, m_scene->StaticMeshes.front(), timestep));

        std::array heaps = { device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap() };
        commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());

        ID3D12Resource* backbuffer = m_swapchain->GetCurrentBackbuffer();
        ID3D12Resource* outputBuffer = m_outputBuffer.Get();

        // TODO: This code is duplicated from renderer.cpp, should be somehow removed
        {
            const Mat4& view = m_scene->Camera.UpdateLookAt();
            const float aspectRatio = m_swapchain->GetWidth() / static_cast<float>(m_swapchain->GetHeight());
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);
            Mat4 viewProjInverse = Mat4(view * projection).Invert();

            const Vec3 eye = m_scene->Camera.GetEyePos();

            RtViewInfoCb viewInfo = {
                .ViewProjInverse = viewProjInverse,
                .CameraWorldPos = Vec4(eye.x, eye.y, eye.z, 1.0f),
            };

            std::memcpy(m_cbViewInfo.GetMapping<RtViewInfoCb>(frameIndex), &viewInfo, sizeof(RtViewInfoCb));
        }

        ImGui::SliderFloat3("Light direction", &m_worldInfo.dirLightDirectionAndIntensity.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Light intensity", &m_worldInfo.dirLightDirectionAndIntensity.w, -1.0f, 1.0f);
        ImGui::SliderFloat3("Light position", &m_worldInfo.dirLightPosition.x, -4.0f, 4.0f);
        std::memcpy(m_cbWorldInfo.GetMapping<RtWorldInfoCb>(frameIndex), &m_worldInfo, sizeof(RtWorldInfoCb));

        commandList->SetComputeRootSignature(m_basicGlobalRS.GetD3D12RootSignature());
        commandList->SetComputeRootConstantBufferView(eGlobalRoot_CbViewInfo, m_cbViewInfo.GetGpuVirtualAddress(frameIndex));
        commandList->SetComputeRootConstantBufferView(eGlobalRoot_CbWorldInfo, m_cbWorldInfo.GetGpuVirtualAddress(frameIndex));
        commandList->SetComputeRootDescriptorTable(eGlobalRoot_SrvTlas, m_rtDescriptors.GpuAt(eDescriptorSlot_TlasSrv));

        // todo: figure this stuff out

        // Setup the raytracing task
        D3D12_DISPATCH_RAYS_DESC desc = {};
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

        commandList->SetPipelineState1(m_rtPso.Get());
        commandList->DispatchRays(&desc);

        // transition output buffer back into copy source
        // copy output resources to swappchain as needed
        {
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(outputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            commandList->CopyResource(backbuffer, outputBuffer);
        }

        // transition resources back as needed
        {
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(outputBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
    }

    //RtAccelerationStructureBuffers RtScene::CreateBLAS(ID3D12GraphicsCommandList4* commandList, std::span<const RTBlasGeometryBuffer> geometryBuffers)
    //{
    //    nv_helpers_dx12::BottomLevelASGenerator blasGenerator;

    //    for (const RTBlasGeometryBuffer& geometry : geometryBuffers)
    //    {
    //        NEB_ASSERT(geometry.VertexStride == sizeof(Vec3), "Only support for vertex stride of {}", sizeof(Vec3));
    //        NEB_ASSERT(geometry.IndexStride == sizeof(uint16_t) || geometry.IndexStride == sizeof(uint32_t), "Incorrect index stride, should be either UINT16 or UINT32");
    //        DXGI_FORMAT indexFormat = (geometry.IndexStride == sizeof(uint16_t)) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    //        blasGenerator.AddVertexBuffer(
    //            geometry.PositionBuffer.Get(), geometry.VertexOffsetInBytes, geometry.NumVertices, geometry.VertexStride,
    //            geometry.IndexBuffer.Get(), geometry.IndexOffsetInBytes, geometry.NumIndices, indexFormat,
    //            nullptr, 0, // we are not using transform buffers in BLAS
    //            true);
    //    }

    //    nri::NRIDevice& device = nri::NRIDevice::Get();

    //    // we do not update geometry (only static meshes)
    //    UINT64 numScratchBytes;
    //    UINT64 numBLASBytes;
    //    blasGenerator.ComputeASBufferSizes(device.GetD3D12Device(), false, &numScratchBytes, &numBLASBytes);

    //    D3D12MA::Allocator* allocator = device.GetResourceAllocator();
    //    D3D12MA::ALLOCATION_DESC allocDesc = {
    //        .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
    //        .HeapType = D3D12_HEAP_TYPE_DEFAULT
    //    };

    //    RtAccelerationStructureBuffers result;

    //    // Create scratch buffer
    //    {
    //        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    //        // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
    //        // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
    //        nri::Rc<D3D12MA::Allocation> allocation;
    //        nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
    //            nullptr,
    //            allocation.GetAddressOf(),
    //            IID_PPV_ARGS(result.ScratchBuffer.GetAddressOf())));
    //    }

    //    // Create ASBuffer
    //    {
    //        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numBLASBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    //        nri::Rc<D3D12MA::Allocation> allocation;
    //        nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    //            nullptr,
    //            allocation.GetAddressOf(),
    //            IID_PPV_ARGS(result.ASBuffer.GetAddressOf())));
    //    }

    //    blasGenerator.Generate(commandList, result.ScratchBuffer.Get(), result.ASBuffer.Get());
    //    return result;
    //}

    //RtAccelerationStructureBuffers RtScene::CreateTLAS(ID3D12GraphicsCommandList4* commandList, std::span<const RTTopLevelInstanceBuffer> instanceBuffers, const RtAccelerationStructureBuffers& updateTlas)
    //{
    //    nv_helpers_dx12::TopLevelASGenerator tlasGenerator;
    //    for (UINT i = 0; i < instanceBuffers.size(); ++i)
    //    {
    //        const RTTopLevelInstanceBuffer& instance = instanceBuffers[i];
    //        tlasGenerator.AddInstance(instance.ASBuffer.Get(), instance.Transformation, i, 0);
    //    }

    //    RtAccelerationStructureBuffers result = updateTlas;
    //    const bool updateOnly = (result.ASBuffer != nullptr);

    //    nri::NRIDevice& device = nri::NRIDevice::Get();

    //    [[maybe_unused]] UINT64 numScratchBytes;
    //    [[maybe_unused]] UINT64 numTLASBytes;
    //    [[maybe_unused]] UINT64 numInstanceDescriptorBytes;
    //    tlasGenerator.ComputeASBufferSizes(device.GetD3D12Device(), true, &numScratchBytes, &numTLASBytes, &numInstanceDescriptorBytes);

    //    D3D12MA::Allocator* allocator = device.GetResourceAllocator();
    //    D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_DEFAULT };

    //    // Create scratch buffer
    //    if (!result.ScratchBuffer)
    //    {
    //        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numScratchBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    //        // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
    //        // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
    //        nri::Rc<D3D12MA::Allocation> allocation;
    //        nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
    //            nullptr,
    //            allocation.GetAddressOf(),
    //            IID_PPV_ARGS(result.ScratchBuffer.GetAddressOf())));
    //    }

    //    // Create ASBuffer
    //    if (!result.ASBuffer)
    //    {
    //        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numTLASBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    //        nri::Rc<D3D12MA::Allocation> allocation;
    //        nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
    //            nullptr,
    //            allocation.GetAddressOf(),
    //            IID_PPV_ARGS(result.ASBuffer.GetAddressOf())));
    //    }

    //    // Instance descriptor buffer
    //    if (!result.InstanceDescriptorBuffer)
    //    {
    //        D3D12MA::ALLOCATION_DESC instanceAllocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };
    //        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(numInstanceDescriptorBytes, D3D12_RESOURCE_FLAG_NONE);

    //        nri::Rc<D3D12MA::Allocation> allocation;
    //        nri::ThrowIfFailed(allocator->CreateResource(&instanceAllocDesc, &desc, D3D12_RESOURCE_STATE_COMMON,
    //            nullptr,
    //            allocation.GetAddressOf(),
    //            IID_PPV_ARGS(result.InstanceDescriptorBuffer.GetAddressOf())));
    //    }

    //    tlasGenerator.Generate(commandList,
    //        result.ScratchBuffer.Get(),
    //        result.ASBuffer.Get(),
    //        result.InstanceDescriptorBuffer.Get(),
    //        updateOnly,                                   // only update AS using D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE
    //        updateOnly ? result.ASBuffer.Get() : nullptr, // previous TLAS for refit
    //        D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE);

    //    return result;
    //}

    BOOL RtScene::InitAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh)
    {
        m_blas = m_asBuilder.CreateBlas(commandList, m_asBuilder.QueryGeometryDescArray(staticMesh));

        nri::RTTopLevelInstance instance = {
            .blasAccelerationStructure = m_blas.accelerationStructureBuffer,
            .transformation = staticMesh.InstanceToWorld,
            .instanceID = 0, // TODO: Figure it out
            .hitGroupIndex = 0, // TODO: Change to actually match SBT entry
            .flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE,
        };

        m_tlas = m_asBuilder.CreateTlas(commandList, std::span(&instance, 1));
        return true;
    }

    BOOL RtScene::UpdateAccelerationStructure(ID3D12GraphicsCommandList4* commandList, const nri::StaticMesh& staticMesh, float timestep)
    {
        // 30 degrees per second
        m_currentRotation.y += 30.0f * timestep;
        Quaternion timeRotation = Quaternion::CreateFromYawPitchRoll(Vec3(
            ToRadians(m_currentRotation.y),
            ToRadians(m_currentRotation.x),
            ToRadians(m_currentRotation.z)));

        // decompose static meshes' SRT
        Vec3 meshScale;
        Vec3 meshTranslation;
        Quaternion meshRotation;

        Mat4 transformation = staticMesh.InstanceToWorld;
        nri::ThrowIfFalse(transformation.Decompose(meshScale, meshRotation, meshTranslation), "Couldnt decompose the transformation matrix for TLAS update");

        Quaternion combinedRotaion = meshRotation * timeRotation;

        Mat4 t = Mat4::CreateTranslation(meshTranslation);
        Mat4 r = Mat4::CreateFromQuaternion(combinedRotaion);
        Mat4 s = Mat4::CreateScale(meshScale);
        Mat4 instanceToWorld = Mat4(s * r * t).Transpose();

        nri::RTTopLevelInstance instance = {
            .blasAccelerationStructure = m_blas.accelerationStructureBuffer,
            .transformation = staticMesh.InstanceToWorld.Transpose(),
            .instanceID = 0,    // TODO: Figure it out
            .hitGroupIndex = 0, // TODO: Change to actually match SBT entry
            .flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE,
        };

        m_tlas = m_asBuilder.CreateTlas(commandList, std::span(&instance, 1), m_tlas);
        return true;
    }

    BOOL RtScene::InitRaytracingPipeline()
    {
        if (!this->InitBasicShaders())
        {
            NEB_LOG_ERROR("Failed to init raytracing shaders");
            return false;
        }

        nri::NRIDevice& device = nri::NRIDevice::Get();

        // TODO: RayTracingPipelineGenerator leaks memory (probably no cleanup for glocal/local root signature)
        //      fix that either in their src or when moving away from Nvidia's helpers

        /* clang-format off */
        nv_helpers_dx12::RayTracingPipelineGenerator pipelineGenerator(device.GetD3D12Device());
        pipelineGenerator.AddLibrary(m_shaderBasic.GetDxcBinaryBlob(),
            { 
                L"RayGen",
                L"Miss",
                L"ClosestHit",
                L"ShadowHit",
                L"ShadowMiss" 
            }
        );

        pipelineGenerator.AddHitGroup(L"HitGroup",          L"ClosestHit");
        pipelineGenerator.AddHitGroup(L"ShadowHitGroup",    L"ShadowHit");

        pipelineGenerator.AddRootSignatureAssociation(m_rayGenRS.GetD3D12RootSignature(),           { L"RayGen" });
        pipelineGenerator.AddRootSignatureAssociation(m_rayMissRS.GetD3D12RootSignature(),          { L"Miss" });
        pipelineGenerator.AddRootSignatureAssociation(m_rayClosestHitRS.GetD3D12RootSignature(),    { L"HitGroup" });
        pipelineGenerator.AddRootSignatureAssociation(m_shadowHitRS.GetD3D12RootSignature(),        { L"ShadowHitGroup" });
        pipelineGenerator.AddRootSignatureAssociation(m_shadowMissRS.GetD3D12RootSignature(),       { L"ShadowMiss" });
        /* clang-format on */

        pipelineGenerator.SetMaxPayloadSize(4 * sizeof(Vec4));   // see HitInfo struct in BasicRt.hlsl
        pipelineGenerator.SetMaxAttributeSize(2 * sizeof(Vec2)); // see Attributes struct in BasicRt.hlsl
        pipelineGenerator.SetMaxRecursionDepth(2);

        m_rtPso = pipelineGenerator.Generate(m_basicGlobalRS.GetD3D12RootSignature());
        nri::ThrowIfFalse(m_rtPso != nullptr);
        nri::ThrowIfFailed(m_rtPso->QueryInterface(m_rtPsoProperties.ReleaseAndGetAddressOf()));
        return true;
    }

    BOOL RtScene::InitBasicShaders()
    {
        // Shader and root-signature related stuff
        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderFilepath = shaderDirectory / "BasicRt.hlsl";

        m_shaderBasic = nri::ShaderCompiler().CompileLibrary(shaderFilepath.string());

        if (!m_shaderBasic.HasBinary())
        {
            NEB_LOG_ERROR("Failed to compile basic RT shader: {}", shaderFilepath.string());
            return false;
        }

        return InitBasicShaderSignatures();
    }

    BOOL RtScene::InitBasicShaderSignatures()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_DESCRIPTOR_RANGE1 tlasSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        m_basicGlobalRS = nri::RootSignature(eGlobalRoot_NumRoots)
                              .AddParamCbv(eGlobalRoot_CbViewInfo, 0)
                              .AddParamCbv(eGlobalRoot_CbWorldInfo, 1)
                              .AddParamDescriptorTable(eGlobalRoot_SrvTlas, std::array{ tlasSrv });

        nri::ThrowIfFalse(m_basicGlobalRS.Init(&device), "failed to init global rs for rt scene");

        D3D12_DESCRIPTOR_RANGE1 outputTextureUav = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        m_rayGenRS = nri::RootSignature(eRaygenRoot_NumRoots)
                         .AddParamDescriptorTable(eRaygenRoot_OutputUav, std::array{ outputTextureUav });

        nri::ThrowIfFalse(m_rayGenRS.Init(&device, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE));

        /* clang-format off */
        const nri::RootSignature emptyLocalRS = nri::RootSignature::Empty(&device, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        m_rayClosestHitRS   = emptyLocalRS;
        m_rayMissRS         = emptyLocalRS;
        m_shadowHitRS       = emptyLocalRS;
        m_shadowMissRS      = emptyLocalRS;
        /* clang-format on */
        return true;
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
        device.GetD3D12Device()->CreateUnorderedAccessView(m_outputBuffer.Get(), nullptr, &uavDesc, m_rtDescriptors.CpuAt(eDescriptorSlot_OutputBufferUav));

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_SRV{
            .Location = m_tlas.accelerationStructureBuffer->GetGPUVirtualAddress()
        };
        device.GetD3D12Device()->CreateShaderResourceView(nullptr, &srvDesc, m_rtDescriptors.CpuAt(eDescriptorSlot_TlasSrv));

        InitConstantBuffers();
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

    void RtScene::InitConstantBuffers()
    {
        nri::ThrowIfFalse(m_cbViewInfo.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(RtViewInfoCb) }));

        nri::ThrowIfFalse(m_cbWorldInfo.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(RtWorldInfoCb) }));

        m_worldInfo = RtWorldInfoCb();
        m_worldInfo.dirLightDirectionAndIntensity = Vec4(-1.0f, 1.0f, 1.0f, 1.0f);
        m_worldInfo.dirLightPosition = Vec3(1.0f, 1.0f, 1.0f);
    }

    BOOL RtScene::InitShaderBindingTable()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        // tlas goes right after output buffer in the heap
        void* pRawDescriptor = reinterpret_cast<void*>(m_rtDescriptors.GpuAt(eDescriptorSlot_OutputBufferUav).ptr);

        m_sbtGenerator.Reset();
        m_sbtGenerator.AddRayGenerationProgram(L"RayGen", { pRawDescriptor });
        m_sbtGenerator.AddMissProgram(L"Miss", {});
        m_sbtGenerator.AddMissProgram(L"ShadowMiss", {});
        m_sbtGenerator.AddHitGroup(L"HitGroup", {});
        m_sbtGenerator.AddHitGroup(L"ShadowHitGroup", {});
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