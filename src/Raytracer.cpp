#include "Raytracer.h"

#include "Nebulae.h" // TODO: Remove

#include "common/Assert.h"
#include "common/Log.h"
#include "core/Math.h"
#include "nri/Device.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"

#include "DXRHelper/nv_helpers_dx12/BottomLevelASGenerator.h"
#include "DXRHelper/nv_helpers_dx12/TopLevelASGenerator.h"
#include "DXRHelper/nv_helpers_dx12/RaytracingPipelineGenerator.h"

#include <array>

namespace Neb
{

    bool RtScene::InitForStaticMesh(const nri::StaticMesh& staticMesh)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        if (device.GetCapabilities().RaytracingSupportTier == nri::ESupportTier_Raytracing::NotSupported)
        {
            NEB_LOG_ERROR("Ray tracing is not supported on this device!");
            return false;
        }

        InitCommandList();
        InitASFences();

        AddStaticMesh(staticMesh);

        InitRaytracingPipeline();
        return true;
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
            IID_PPV_ARGS(m_accelerationStructFence.ReleaseAndGetAddressOf())));
    }

    RtAccelerationStructureBuffers RtScene::CreateBLAS(std::span<const RtBLASGeometryBuffer> geometryBuffers)
    {
        nv_helpers_dx12::BottomLevelASGenerator blasGenerator;

        for (const RtBLASGeometryBuffer& geometry : geometryBuffers)
        {
            NEB_ASSERT(geometry.VertexStride == sizeof(Vec3), "Only support for vertex stride of {}", sizeof(Vec3));
            blasGenerator.AddVertexBuffer(
                geometry.Buffer.Get(),
                geometry.VertexOffset, geometry.NumVertices, geometry.VertexStride,
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

        // TODO: Figure out do we need to add transition to D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE for vertex buffer?
        /*std::array barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
            CD3DX12_RESOURCE_BARRIER::Transition(
                m_depthStencilBuffer.GetBufferResource(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE),
        }; */

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
            result.InstanceDescriptorBuffer.Get());
        return result;
    }

    bool RtScene::InitAccelerationStructure(const nri::StaticMesh& staticMesh)
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
                    .Buffer = submesh.AttributeBuffers[nri::eAttributeType_Position],
                    .VertexStride = static_cast<UINT>(submesh.AttributeStrides[nri::eAttributeType_Position]),
                    .VertexOffset = 0,
                    .NumVertices = submesh.NumVertices,
                });
            }
            blas = CreateBLAS(geometryBuffers);

            // Create TLAS
            // TODO: Temporarily hardcoded instance transformations
            static std::array InstanceTransformations = {
                Mat4()
            };

            std::vector<RtTLASInstanceBuffer> instanceBuffers;
            instanceBuffers.reserve(InstanceTransformations.size()); // N instances

            for (size_t instanceID = 0; instanceID < InstanceTransformations.size(); ++instanceID)
            {
                instanceBuffers.push_back(RtTLASInstanceBuffer{
                    .ASBuffer = blas.ASBuffer,
                    .Transformation = InstanceTransformations[instanceID],
                });
            }
            tlas = CreateTLAS(instanceBuffers);
        }
        nri::ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* pCommandList = m_commandList.Get();
        ID3D12CommandQueue* queue = device.GetCommandQueue(nri::eCommandContextType_Graphics);
        UINT fenceValue = m_fenceValue++;

        // This stuff breaks -> idk why but buffers seem to be fine
        queue->ExecuteCommandLists(1, &pCommandList);
        queue->Signal(m_accelerationStructFence.Get(), fenceValue);
        {
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            NEB_ASSERT(fenceEvent, "Failed to create HANDLE for event");

            // Wait until the fence is completed.
            nri::ThrowIfFailed(m_accelerationStructFence->SetEventOnCompletion(fenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }

        commandAllocatorPool.DiscardAllocator(commandAllocator, m_accelerationStructFence.Get(), fenceValue);

        // Member assignments
        m_tlas = tlas;
        return true;
    }

    bool RtScene::InitRaytracingPipeline()
    {
        // Shader and root-signature related stuff
        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderFilepath = shaderDirectory / "BasicRt.hlsl";

        nri::ThrowIfFalse(this->InitRayGen(shaderFilepath));
        nri::ThrowIfFalse(this->InitRayClosestHit(shaderFilepath));
        nri::ThrowIfFalse(this->InitRayMiss(shaderFilepath));

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

        pipelineGenerator.SetMaxPayloadSize(sizeof(Vec4));   // see HitInfo struct in BasicRt.hlsl
        pipelineGenerator.SetMaxAttributeSize(sizeof(Vec2)); // see Attributes struct in BasicRt.hlsl
        pipelineGenerator.SetMaxRecursionDepth(1);

        m_rtPso = pipelineGenerator.Generate();
        nri::ThrowIfFalse(m_rtPso != nullptr);
        nri::ThrowIfFailed(m_rtPso->QueryInterface(m_rtPsoProperties.ReleaseAndGetAddressOf()));
        return true;
    }

    bool RtScene::InitRayGen(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayGen = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("RayGen", shaderModel, nri::EShaderType::RayGen));


        D3D12_DESCRIPTOR_RANGE1 outputTextureUav = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
        m_rayGenRS = nri::RootSignature()
                         .AddParamDescriptorTable(std::array{ outputTextureUav })
                         .AddParamSrv(0);

        nri::ThrowIfFalse(m_rayGenRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE));
        return true;
    }

    bool RtScene::InitRayClosestHit(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayClosestHit = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("ClosestHit", shaderModel, nri::EShaderType::RayClosestHit));

        m_rayClosestHitRS = nri::RootSignature();
        nri::ThrowIfFalse(m_rayClosestHitRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE));
        return true;
    }

    bool RtScene::InitRayMiss(const std::filesystem::path& filepath, nri::EShaderModel shaderModel)
    {
        m_rayMiss = nri::ShaderCompiler().CompileShader(filepath.string(),
            nri::ShaderCompilationDesc("Miss", shaderModel, nri::EShaderType::RayMiss));

        m_rayMissRS = nri::RootSignature();
        nri::ThrowIfFalse(m_rayMissRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE));
        return true;
    }

}