#include "DeferredRenderer.h"

#include "Nebulae.h" // TODO: Needed for assets directory, should be removed
#include "core/Math.h"
#include "nri/Device.h"
#include "nri/DescriptorHeap.h"
#include "nri/ShaderCompiler.h"

#include <format>

namespace Neb
{

    bool DeferredRenderer::Init(UINT width, UINT height, nri::Swapchain* swapchain)
    {   
        m_width = width;
        m_height = height;
        m_swapchain = swapchain;

        InitGbuffers();
        InitGbufferHeaps();
        InitGbufferDepthStencilBuffer();
        InitGbufferDepthSrv();
        InitGbufferShadersAndRootSignatures();
        InitGbufferPipelineState();
        InitGbufferInstanceCb();

        InitPBRResources();
        InitPBRDescriptorHeaps();
        InitPBRShadersAndRootSignature();
        InitPBRConstantBuffers();
        InitPBRPipeline();

        InitHDRTonemapShadersAndRootSignature();
        InitHDRTonemapPipeline(m_swapchain->GetFormat());

        return true;
    }

    void DeferredRenderer::Resize(UINT width, UINT height)
    {
        // avoid reallocating everything for no reason
        if (width == m_width && height == m_height)
            return;

        m_width = width;
        m_height = height;

        nri::ThrowIfFalse(m_depthStencilBuffer.Resize(width, height), "Failed to resize depth stencil buffer");
        InitGbuffers();
        InitGbufferHeaps();
        InitGbufferDepthSrv();

        InitPBRResources();
        InitPBRDescriptorHeaps();
    }

    void DeferredRenderer::SubmitCommandsGbuffer(const RenderInfo& info)
    {
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        nri::NRIDevice& device = nri::NRIDevice::Get();
        UINT width = m_width;
        UINT height = m_height;

        // Rendering context
        ID3D12GraphicsCommandList4* commandList = info.commandList;
        {
            SetupDescriptorHeaps(commandList);

            TransitionGbuffers(commandList,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
            {
                SetupGbufferRtvs(commandList);
                SetupViewports(commandList);
            }

            // Setup PSO
            commandList->SetGraphicsRootSignature(m_gbufferRS.GetD3D12RootSignature());
            commandList->SetGraphicsRootConstantBufferView(DEFERRED_RENDERER_ROOTS_INSTANCE_INFO, m_cbInstance.GetGpuVirtualAddress(info.frameIndex));
            commandList->SetPipelineState(m_pipelineState.Get());

            // pre-calculate view/projection before looping
            const Mat4& view = info.scene->Camera.UpdateLookAt();
            const float aspectRatio = width / static_cast<float>(height);
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);

            // Now finally submit commands per-mesh
            for (nri::StaticMesh& staticMesh : info.scene->StaticMeshes)
            {
                const size_t numSubmeshes = staticMesh.Submeshes.size();
                NEB_ASSERT(numSubmeshes == staticMesh.SubmeshMaterials.size(),
                    "Static mesh is invalid. It has {} submeshes while only {} materials",
                    numSubmeshes, staticMesh.SubmeshMaterials.size());

                for (size_t i = 0; i < numSubmeshes; ++i)
                {
                    nri::StaticSubmesh& submesh = staticMesh.Submeshes[i];
                    nri::Material& material = staticMesh.SubmeshMaterials[i];

                    CbInstanceInfo cbInstanceInfo = CbInstanceInfo{
                        .InstanceToWorld = staticMesh.InstanceToWorld,
                        .ViewProj = view * projection,
                        .MaterialFlags = material.Flags // TODO: Would be nice to have a separate constant buffer for material properties
                    };
                    std::memcpy(m_cbInstance.GetMapping<CbInstanceInfo>(info.frameIndex), &cbInstanceInfo, sizeof(CbInstanceInfo));

                    commandList->SetGraphicsRootDescriptorTable(DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES, material.SrvRange.GpuAddress);

                    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    commandList->IASetVertexBuffers(0, nri::eAttributeType_NumTypes, submesh.AttributeViews.data());
                    commandList->IASetIndexBuffer(&submesh.IBView);
                    commandList->DrawIndexedInstanced(submesh.NumIndices, 1, 0, 0, 0);
                }
            }

            // transition every gbuffer from render state to common
            TransitionGbuffers(commandList,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                D3D12_RESOURCE_STATE_COMMON);
        }
    }

    void DeferredRenderer::SubmitCommandsPBRLighting(const RenderInfo& info)
    {
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        nri::NRIDevice& device = nri::NRIDevice::Get();
        UINT width = m_width;
        UINT height = m_height;

        ID3D12GraphicsCommandList4* commandList = info.commandList;
        {
            SetupDescriptorHeaps(commandList);
            SetupViewports(commandList);

            ID3D12Resource* resultBuffer = m_hdrResult->GetResource();
            {
                std::array barriers = { CD3DX12_RESOURCE_BARRIER::Transition(resultBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

                TransitionGbuffers(commandList,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            commandList->SetComputeRootSignature(m_pbrRS.GetD3D12RootSignature());
            commandList->SetComputeRootConstantBufferView(PBR_ROOT_CB_VIEW_DATA, m_cbViewData.GetGpuVirtualAddress(info.frameIndex));
            commandList->SetComputeRootConstantBufferView(PBR_ROOT_CB_LIGHT_ENV, m_cbLightEnv.GetGpuVirtualAddress(info.frameIndex));
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_GBUFFERS, m_gbufferSrvHeap.GpuAddress);
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_SCENE_DEPTH, m_depthSrv.GpuAddress);
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_HDR_OUTPUT_UAV, m_pbrSrvUavHeap.GpuAt(HDR_UAV_INDEX));
            commandList->SetPipelineState(m_pbrPipeline.Get());

            const Mat4& view = info.scene->Camera.UpdateLookAt();
            const float aspectRatio = width / static_cast<float>(height);
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);

            CbViewData viewData = {
                .viewInv = view.Invert(),
                .projInv = projection.Invert(),
            };
            std::memcpy(m_cbViewData.GetMapping(info.frameIndex), &viewData, sizeof(CbViewData));

            CbLightEnvironment lightEnv = {
                .intensity = 4.0f,
                .direction = Vec3(0.0f, -1.0f, 0.0f),
                .radiance = Vec3(4.0f, 2.5f, 2.7f),
            };
            std::memcpy(m_cbLightEnv.GetMapping(info.frameIndex), &lightEnv, sizeof(CbLightEnvironment));

            // Fullscreen triangle
            //commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            //commandList->DrawInstanced(3, 1, 0, 0);
            commandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1); 
            {
                TransitionGbuffers(commandList,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON);

                auto pbrBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resultBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                commandList->ResourceBarrier(1, &pbrBarrier);
            }
        }
    }

    void DeferredRenderer::SubmitCommandsHDRTonemapping(ID3D12GraphicsCommandList4* commandList)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::Swapchain* swapchain = m_swapchain;
        UINT width = swapchain->GetWidth();
        UINT height = swapchain->GetHeight();

        //commandList = commandList;
        {
            SetupDescriptorHeaps(commandList);
            
            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, width, height);
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, width, height);
            commandList->RSSetScissorRects(1, &scissorRect);

            ID3D12Resource* backbuffer = swapchain->GetCurrentBackbuffer();

            // Transition HDR input and backbuffer output
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(this->GetHDROutputResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv = swapchain->GetBackbufferRtvHandle(swapchain->GetCurrentBackbufferIndex());
            commandList->OMSetRenderTargets(1, &backbufferRtv, FALSE, nullptr);

            commandList->SetGraphicsRootSignature(m_tonemapRS.GetD3D12RootSignature());
            commandList->SetGraphicsRootDescriptorTable(TONEMAP_ROOT_HDR_INPUT, m_pbrSrvUavHeap.GpuAt(HDR_SRV_INDEX));
            commandList->SetPipelineState(m_tonemapPipeline.Get());

            // Fullscreen triangle
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->DrawInstanced(3, 1, 0, 0);
            {
                // Transition HDR input and backbuffer output
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(this->GetHDROutputResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }
        }
    }

    void DeferredRenderer::TransitionGbuffers(ID3D12GraphicsCommandList4* commandList,
        D3D12_RESOURCE_STATES prev,
        D3D12_RESOURCE_STATES next,
        D3D12_RESOURCE_STATES depthPrev,
        D3D12_RESOURCE_STATES depthNext)
    {
        std::array barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferAlbedo(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferNormal(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferRoughnessMetalness(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferWorldPos(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(m_depthStencilBuffer.GetBufferResource(), depthPrev, depthNext),
        };
        commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }

    void DeferredRenderer::SetupDescriptorHeaps(ID3D12GraphicsCommandList4* commandList)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        
        std::array shaderVisibleHeaps = {
            device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap(),
            device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).GetHeap(),
        };
        commandList->SetDescriptorHeaps(static_cast<UINT>(shaderVisibleHeaps.size()), shaderVisibleHeaps.data());
    }

    void DeferredRenderer::SetupGbufferRtvs(ID3D12GraphicsCommandList4* commandList)
    {
        // Clear gbuffers
        {
            static const Neb::Vec4 gbufferClearColor = Neb::Vec4(0.0f);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_NORMAL), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), &gbufferClearColor.x, 0, nullptr);
        }

        // set rtvs
        const nri::DescriptorHeapAllocation& dsvDescriptor = m_depthStencilBuffer.GetDSV();
        commandList->ClearDepthStencilView(dsvDescriptor.CpuAt(0), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(
            GBUFFER_SLOT_NUM_SLOTS,
            &m_gbufferRtvHeap.CpuAddress, TRUE, &m_depthStencilBuffer.GetDSV().CpuAddress);
    }

    void DeferredRenderer::SetupViewports(ID3D12GraphicsCommandList4* commandList)
    {
        D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_width, m_height);
        commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, m_width, m_height);
        commandList->RSSetScissorRects(1, &scissorRect);
    }

    void DeferredRenderer::InitGbuffers()
    {
        UINT width = m_width;
        UINT height = m_height;

        static constexpr auto CreateGbufferResource = [](
                                                          UINT width,
                                                          UINT height,
                                                          DXGI_FORMAT format) -> nri::Rc<D3D12MA::Allocation>
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();

            D3D12MA::Allocator* allocator = device.GetResourceAllocator();
            D3D12MA::ALLOCATION_DESC allocDesc = {
                .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                .HeapType = D3D12_HEAP_TYPE_DEFAULT,
            };

            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            Vec4 v = Vec4(0.0f);
            D3D12_CLEAR_VALUE clearValue = { .Format = format };
            std::memcpy(clearValue.Color, &v.x, sizeof(clearValue.Color));

            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(
                                   &allocDesc,
                                   &resourceDesc,
                                   D3D12_RESOURCE_STATE_COMMON, &clearValue,
                                   allocation.GetAddressOf(), nri::NullRIID, nullptr),
                "Failed to create gbuffer");
            return allocation;
        };

        m_gbufferAlbedo = CreateGbufferResource(width, height, DXGI_FORMAT_R11G11B10_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferAlbedo, "Albedo GBuffer DXGI_FORMAT_R11G11B10_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferAlbedo->GetResource(), "Albedo GBuffer DXGI_FORMAT_R11G11B10_FLOAT");

        m_gbufferNormal = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferNormal, "Normal GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferNormal->GetResource(), "Normal GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");

        m_gbufferRoughnessMetalness = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferRoughnessMetalness, "Roughness/Metalness GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferRoughnessMetalness->GetResource(), "Roughness/Metalness GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");

        m_gbufferWorldPos = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferWorldPos, "World Position GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferWorldPos->GetResource(), "World Position GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");
    }

    void DeferredRenderer::InitGbufferHeaps()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        if (m_gbufferSrvHeap.IsNull())
        {
            m_gbufferSrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(EGbufferSlot::GBUFFER_SLOT_NUM_SLOTS);
            NEB_ASSERT(!m_gbufferSrvHeap.IsNull(), "Failed to allocate SRV descriptors");
        }

        if (m_gbufferRtvHeap.IsNull())
        {
            m_gbufferRtvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptors(EGbufferSlot::GBUFFER_SLOT_NUM_SLOTS);
            NEB_ASSERT(!m_gbufferRtvHeap.IsNull(), "Failed to allocate RTV descriptors");
        }

        constexpr auto AllocateGbufferSrv = [](ID3D12Resource* resource,
                                                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                DXGI_FORMAT format,
                                                D3D12_SRV_DIMENSION dim = D3D12_SRV_DIMENSION_TEXTURE2D)
        {
            auto srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D = D3D12_TEX2D_SRV{ .MipLevels = 1 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
        };

        AllocateGbufferSrv(this->GetGbufferAlbedo(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), DXGI_FORMAT_R11G11B10_FLOAT);
        AllocateGbufferSrv(this->GetGbufferNormal(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_NORMAL), DXGI_FORMAT_R16G16B16A16_FLOAT);
        AllocateGbufferSrv(this->GetGbufferRoughnessMetalness(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), DXGI_FORMAT_R16G16_FLOAT);
        AllocateGbufferSrv(this->GetGbufferWorldPos(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), DXGI_FORMAT_R16G16B16A16_FLOAT);

        constexpr auto AllocateGbufferRtv = [](ID3D12Resource* resource,
                                                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                DXGI_FORMAT format,
                                                D3D12_RTV_DIMENSION dim = D3D12_RTV_DIMENSION_TEXTURE2D)
        {
            auto rtvDesc = D3D12_RENDER_TARGET_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Texture2D = D3D12_TEX2D_RTV{ .MipSlice = 0, .PlaneSlice = 0 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateRenderTargetView(resource, &rtvDesc, cpuHandle);
        };

        AllocateGbufferRtv(this->GetGbufferAlbedo(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), DXGI_FORMAT_R11G11B10_FLOAT);
        AllocateGbufferRtv(this->GetGbufferNormal(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_NORMAL), DXGI_FORMAT_R16G16B16A16_FLOAT);
        AllocateGbufferRtv(this->GetGbufferRoughnessMetalness(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), DXGI_FORMAT_R16G16_FLOAT);
        AllocateGbufferRtv(this->GetGbufferWorldPos(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    void DeferredRenderer::InitGbufferDepthStencilBuffer()
    {
        UINT width = m_width;
        UINT height = m_height;
        nri::ThrowIfFalse(m_depthStencilBuffer.Init(width, height, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            "Failed to create depth stencil buffer for deferred rendering");

        NEB_SET_HANDLE_NAME(m_depthStencilBuffer.GetBufferResource(), "Deferred scene depth");
    }

    void DeferredRenderer::InitGbufferDepthSrv()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        m_depthSrv = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(1);

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {
            .Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{ .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0 }
        };
        device.GetD3D12Device()->CreateShaderResourceView(m_depthStencilBuffer.GetBufferResource(), &desc, m_depthSrv.CpuAddress);
    }

    void DeferredRenderer::InitGbufferShadersAndRootSignatures()
    {
        nri::ShaderCompiler* compiler = nri::ShaderCompiler::Get();

        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::string shaderFilepath = (shaderDir / "deferred_gbuffers.hlsl").string();

        m_vsGbuffer = compiler->CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex),
            nri::eShaderCompilationFlag_None);

        m_psGbuffer = compiler->CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel),
            nri::eShaderCompilationFlag_None);

        D3D12_DESCRIPTOR_RANGE1 materialTexturesRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nri::eMaterialTextureType_NumTypes, 0, 0);
        m_gbufferRS = nri::RootSignature(DEFERRED_RENDERER_ROOTS_NUM_ROOTS, 1)
                          .AddParamCbv(DEFERRED_RENDERER_ROOTS_INSTANCE_INFO, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL)
                          .AddParamDescriptorTable(DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES, std::array{ materialTexturesRange }, D3D12_SHADER_VISIBILITY_PIXEL)
                          .AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));

        nri::ThrowIfFalse(m_gbufferRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT));
    }

    void DeferredRenderer::InitGbufferPipelineState()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_gbufferRS.GetD3D12RootSignature();
        psoDesc.VS = m_vsGbuffer.GetBinaryBytecode();
        psoDesc.PS = m_psGbuffer.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // glTF 2.0 spec: the winding order triangle faces is CCW
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = D3D12_INPUT_LAYOUT_DESC{
            .pInputElementDescs = nri::StaticMeshInputLayout.data(),
            .NumElements = nri::StaticMeshInputLayout.size(),
        };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = GBUFFER_SLOT_NUM_SLOTS;
        psoDesc.RTVFormats[GBUFFER_SLOT_ALBEDO] = DXGI_FORMAT_R11G11B10_FLOAT;
        psoDesc.RTVFormats[GBUFFER_SLOT_NORMAL] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.RTVFormats[GBUFFER_SLOT_ROUGHNESS_METALNESS] = DXGI_FORMAT_R16G16_FLOAT;
        psoDesc.RTVFormats[GBUFFER_SLOT_WORLD_POS] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(device.GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitGbufferInstanceCb()
    {
        nri::ThrowIfFalse(m_cbInstance.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbInstanceInfo) }));
    }

    void DeferredRenderer::InitPBRResources()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT,
        };

        DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, m_width, m_height, 1, 1);
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        /*Vec4 v = Vec4(0.0f);
        D3D12_CLEAR_VALUE clearValue = { .Format = format };
        std::memcpy(clearValue.Color, &v.x, sizeof(clearValue.Color));*/

        nri::ThrowIfFailed(allocator->CreateResource(
                               &allocDesc,
                               &resourceDesc,
                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                               m_hdrResult.ReleaseAndGetAddressOf(), nri::NullRIID, nullptr),
            "Failed to create PBR result resource");

        NEB_SET_HANDLE_NAME(m_hdrResult, "HDR Output Texture2D DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_hdrResult->GetResource(), "HDR Output Texture2D DXGI_FORMAT_R16G16B16A16_FLOAT");
    }

    void DeferredRenderer::InitPBRDescriptorHeaps()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        if (m_pbrSrvUavHeap.IsNull())
        {
            m_pbrSrvUavHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(2);
            NEB_ASSERT(!m_pbrSrvUavHeap.IsNull(), "Failed to allocate SRV descriptors");
        }

        constexpr auto AllocateSrv = [](ID3D12Resource* resource,
                                                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                DXGI_FORMAT format,
                                                D3D12_SRV_DIMENSION dim = D3D12_SRV_DIMENSION_TEXTURE2D)
        {
            auto srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D = D3D12_TEX2D_SRV{ .MipLevels = 1 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
        };

        constexpr auto AllocateUav = [](ID3D12Resource* resource,
                                         D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                         DXGI_FORMAT format,
                                         D3D12_UAV_DIMENSION dim = D3D12_UAV_DIMENSION_TEXTURE2D)
        {
            auto uavDesc = D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Texture2D = D3D12_TEX2D_UAV{ .MipSlice = 0, .PlaneSlice = 0 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);
        };

        AllocateSrv(this->GetHDROutputResource(), m_pbrSrvUavHeap.CpuAt(HDR_SRV_INDEX), DXGI_FORMAT_R16G16B16A16_FLOAT);
        AllocateUav(this->GetHDROutputResource(), m_pbrSrvUavHeap.CpuAt(HDR_UAV_INDEX), DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    void DeferredRenderer::InitPBRShadersAndRootSignature()
    {
        nri::ShaderCompiler* compiler = nri::ShaderCompiler::Get();

        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";

        /*m_vsPBR = compiler->CompileShader(
            (shaderDir / "fullscreen_triangle_vs.hlsl").string(),
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex),
            nri::eShaderCompilationFlag_None);*/

        m_csPBR = compiler->CompileShader(
            (shaderDir / "deferred_pbr.hlsl").string(),
            nri::ShaderCompilationDesc("CSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Compute));

        D3D12_DESCRIPTOR_RANGE1 gbufferSrvRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBUFFER_SLOT_NUM_SLOTS, 0, 1);
        D3D12_DESCRIPTOR_RANGE1 sceneDepthSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        D3D12_DESCRIPTOR_RANGE1 hdrOutputUav = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
        m_pbrRS = nri::RootSignature(PBR_ROOT_NUM_ROOTS)
                      .AddParamCbv(PBR_ROOT_CB_VIEW_DATA, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC)
                      .AddParamCbv(PBR_ROOT_CB_LIGHT_ENV, 1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC)
                      .AddParamDescriptorTable(PBR_ROOT_GBUFFERS, std::array{ gbufferSrvRange })
                      .AddParamDescriptorTable(PBR_ROOT_SCENE_DEPTH, std::array{ sceneDepthSrv })
                      .AddParamDescriptorTable(PBR_ROOT_HDR_OUTPUT_UAV, std::array{ hdrOutputUav });
        nri::ThrowIfFalse(m_pbrRS.Init(&nri::NRIDevice::Get()));
    }

    void DeferredRenderer::InitPBRConstantBuffers()
    {
        nri::ThrowIfFalse(m_cbViewData.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbViewData) }));

        nri::ThrowIfFalse(m_cbLightEnv.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbLightEnvironment) }));
    }

    void DeferredRenderer::InitPBRPipeline()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pbrRS.GetD3D12RootSignature();
        psoDesc.CS = m_csPBR.GetBinaryBytecode();
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(m_pbrPipeline.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitHDRTonemapShadersAndRootSignature()
    {
        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";

        m_vsTonemap = nri::ShaderCompiler::Get()->CompileShader(
            (shaderDir / "fullscreen_triangle_vs.hlsl").string(), nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex));

        m_psTonemap = nri::ShaderCompiler::Get()->CompileShader(
            (shaderDir / "tonemapping.hlsl").string(), nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel));

        D3D12_DESCRIPTOR_RANGE1 tonemapHdrInput = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        m_tonemapRS = nri::RootSignature(TONEMAP_ROOT_NUM_ROOTS, 1)
                          .AddParamDescriptorTable(TONEMAP_ROOT_HDR_INPUT, std::array{ tonemapHdrInput }, D3D12_SHADER_VISIBILITY_PIXEL)
                          .AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));

        nri::ThrowIfFalse(m_tonemapRS.Init(&nri::NRIDevice::Get()));
    }

    void DeferredRenderer::InitHDRTonemapPipeline(DXGI_FORMAT outputFormat)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_tonemapRS.GetD3D12RootSignature();
        psoDesc.VS = m_vsTonemap.GetBinaryBytecode();
        psoDesc.PS = m_psTonemap.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = D3D12_INPUT_LAYOUT_DESC();
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = outputFormat;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_tonemapPipeline.ReleaseAndGetAddressOf())),
            "Failed to create tonemapping pipeline");
    }

} // Neb namespace