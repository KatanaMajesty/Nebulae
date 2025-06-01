#include "DeferredRenderer.h"

#include "Nebulae.h" // TODO: Needed for assets directory, should be removed
#include "core/Math.h"
#include "nri/Device.h"
#include "nri/DescriptorHeap.h"
#include "nri/ShaderCompiler.h"

#include <format>

namespace Neb
{

    bool DeferredRenderer::Init(UINT width, UINT height)
    {   
        m_width = width;
        m_height = height;
        InitGbuffers();
        InitGbufferHeaps();
        InitDepthStencilBuffer();
        InitShadersAndRootSignatures();
        InitPipelineState();
        InitInstanceCb();
        return true;
    }

    void DeferredRenderer::Resize(UINT width, UINT height)
    {
        // avoid reallocating everything for no reason
        if (width == m_width && height == m_height)
            return;

        m_width = width;
        m_height = height;
        InitGbuffers();
        InitGbufferHeaps();
        nri::ThrowIfFalse(m_depthStencilBuffer.Resize(width, height), "Failed to resize depth stencil buffer");
    }

    void DeferredRenderer::SubmitCommands(const RenderInfo& info)
    {
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        nri::NRIDevice& device = nri::NRIDevice::Get();
        UINT width = m_width;
        UINT height = m_height;

        // Rendering context
        ID3D12GraphicsCommandList4* commandList = info.commandList;
        {
            std::array shaderVisibleHeaps = {
                device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap(),
                device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).GetHeap(),
            };
            commandList->SetDescriptorHeaps(static_cast<UINT>(shaderVisibleHeaps.size()), shaderVisibleHeaps.data());

            // transition every gbuffer to render target state
            {
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferAlbedo(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferNormal(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferRoughnessMetalness(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
                    CD3DX12_RESOURCE_BARRIER::Transition(m_depthStencilBuffer.GetBufferResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE),
                };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }

            // Clear gbuffers
            {
                static const Neb::Vec4 gbufferClearColor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
                commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), &gbufferClearColor.x, 0, nullptr);
                commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_NORMAL), &gbufferClearColor.x, 0, nullptr);
                commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), &gbufferClearColor.x, 0, nullptr);
            }

            // set rtvs
            const nri::DescriptorHeapAllocation& dsvDescriptor = m_depthStencilBuffer.GetDSV();
            commandList->ClearDepthStencilView(dsvDescriptor.CpuAt(0), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            commandList->OMSetRenderTargets(
                GBUFFER_SLOT_NUM_SLOTS,
                &m_gbufferRtvHeap.CpuAddress, TRUE, &m_depthStencilBuffer.GetDSV().CpuAddress);

            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, width, height);
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, width, height);
            commandList->RSSetScissorRects(1, &scissorRect);

            // Setup PSO
            commandList->SetGraphicsRootSignature(m_gbufferRS.GetD3D12RootSignature());
            commandList->SetPipelineState(m_pipelineState.Get());

            // Bind instance info once, will be updated later
            commandList->SetGraphicsRootConstantBufferView(DEFERRED_RENDERER_ROOTS_INSTANCE_INFO, m_cbInstance.GetGpuVirtualAddress(info.frameIndex));

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
            {
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferAlbedo(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferNormal(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferRoughnessMetalness(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(m_depthStencilBuffer.GetBufferResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON),
                };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }
        }
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
    }

    void DeferredRenderer::InitDepthStencilBuffer()
    {
        UINT width = m_width;
        UINT height = m_height;
        nri::ThrowIfFalse(m_depthStencilBuffer.Init(width, height), "Failed to create depth stencil buffer for deferred rendering");
    }

    void DeferredRenderer::InitShadersAndRootSignatures()
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

    void DeferredRenderer::InitPipelineState()
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
        psoDesc.DSVFormat = m_depthStencilBuffer.GetFormat();
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(device.GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitInstanceCb()
    {
        nri::ThrowIfFalse(m_cbInstance.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbInstanceInfo) }));
    }

   

} // Neb namespace