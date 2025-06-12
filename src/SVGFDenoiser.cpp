#include "SVGFDenoiser.h"

#include "nri/Device.h"
#include "nri/ShaderCompiler.h"
#include "nri/imgui/UiContext.h"
#include "Nebulae.h"

#include <filesystem>

namespace Neb
{

    bool SVGFDenoiser::Init(UINT width, UINT height)
    {
        NEB_ASSERT(!IsInitialized());
        m_initialized = true;

        m_width = width;
        m_height = height;

        InitSVGFShadersAndPSO();
        InitSVGFResources();
        InitSVGFDescriptors();
        return true;
    }

    bool SVGFDenoiser::Resize(UINT width, UINT height)
    {
        NEB_ASSERT(IsInitialized());
        m_width = width;
        m_height = height;

        InitSVGFResources();
        InitSVGFDescriptors();
        return false;
    }

    void SVGFDenoiser::BeginFrame(UINT frameIndex)
    {
        m_currentIndex = frameIndex & 1;
        m_historyIndex = m_currentIndex ^ 1;
    }

    void SVGFDenoiser::EndFrame()
    {
    }

    void SVGFDenoiser::ResetHistory(ID3D12GraphicsCommandList4* commandList)
    {
        std::array barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(GetHistoryRadianceTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentRadianceTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
        };
        commandList->ResourceBarrier(UINT(barriers.size()), barriers.data());
        {
            commandList->CopyResource(GetHistoryRadianceTexture(), GetCurrentRadianceTexture());
        }
        barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(GetHistoryRadianceTexture(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
            CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentRadianceTexture(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
        };
        commandList->ResourceBarrier(UINT(barriers.size()), barriers.data());
    }

    void SVGFDenoiser::SubmitTemporalAccumulation(ID3D12GraphicsCommandList4* commandList)
    {
        NEB_ASSERT(IsInitialized());

        UINT width = m_width;
        UINT height = m_height;
        {
            static float alpha = 0.9f;
            ImGui::Begin("SVGF");
            ImGui::SliderFloat("Alpha", &alpha, 1e-4f, 1.0f);
            SVGFTemporalConstants temporal = {
                .resolution = { width, height },
                .depthSigma = 0.002f,
                .alpha = alpha,       // tweakable stability var
                .varianceEps = 1e-4f, // small value
            };
            ImGui::End();
            
            commandList->SetPipelineState(m_svgfTemporalPSO.Get());
            commandList->SetComputeRootSignature(m_svgfTemporalRS.GetD3D12RootSignature());
            // SVGF_TEMPORAL_ROOT_CONSTANTS = 0,
            // SVGF_TEMPORAL_ROOT_RADIANCE_HISTORY_SRV,
            // SVGF_TEMPORAL_ROOT_DEPTH_CURRENT_SRV,
            // SVGF_TEMPORAL_ROOT_DEPTH_HISTORY_SRV,
            // SVGF_TEMPORAL_ROOT_NORMAL_CURRENT_SRV,
            // SVGF_TEMPORAL_ROOT_NORMAL_HISTORY_SRV,
            // SVGF_TEMPORAL_ROOT_MOMENT_HISTORY_SRV,
            // SVGF_TEMPORAL_ROOT_RADIANCE_CURRENT_UAV,
            // SVGF_TEMPORAL_ROOT_MOMENT_CURRENT_UAV,
            // SVGF_TEMPORAL_ROOT_VARIANCE_UAV,
            // SVGF_TEMPORAL_ROOT_NUM_ROOTS,
            commandList->SetComputeRoot32BitConstants(SVGF_TEMPORAL_ROOT_CONSTANTS, sizeof(SVGFTemporalConstants) / sizeof(UINT), &temporal, 0);
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_RADIANCE_HISTORY_SRV, GetHistoryRadianceSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_DEPTH_CURRENT_SRV, GetCurrentDepthSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_DEPTH_HISTORY_SRV, GetHistoryDepthSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_NORMAL_CURRENT_SRV, GetCurrentNormalSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_NORMAL_HISTORY_SRV, GetHistoryNormalSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_MOMENT_HISTORY_SRV, GetHistoryMomentSrv());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_RADIANCE_CURRENT_UAV, GetCurrentRadianceUav());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_MOMENT_CURRENT_UAV, GetCurrentMomentUav());
            commandList->SetComputeRootDescriptorTable(SVGF_TEMPORAL_ROOT_VARIANCE_UAV, GetVarianceUav());
            commandList->Dispatch(width / 8, height / 8, 1);
        }
    }

    void SVGFDenoiser::SubmitATrousComputeWavelet()
    {
        NEB_ASSERT(IsInitialized());
    }

    void SVGFDenoiser::InitSVGFShadersAndPSO()
    {
        NEB_ASSERT(IsInitialized());

        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderSVGFTemporalFilepath = shaderDirectory / "svgf_temporal.hlsl";
        const std::filesystem::path shaderSVGFATrousFilepath = shaderDirectory / "svgf_atrous.hlsl";

        m_csTemporalSVGF = nri::ShaderCompiler::Get()->CompileShader(
            shaderSVGFTemporalFilepath.string(),
            nri::ShaderCompilationDesc("CSMain_SVGF_Temporal", nri::EShaderModel::sm_6_5, nri::EShaderType::Compute));

        m_csATrousSVGF = nri::ShaderCompiler::Get()->CompileShader(
            shaderSVGFATrousFilepath.string(),
            nri::ShaderCompilationDesc("CSMain_SVGF_ATrous", nri::EShaderModel::sm_6_5, nri::EShaderType::Compute));

        NEB_ASSERT(m_csTemporalSVGF.HasBinary(), "Failed to compile SVGF temporal compute shader: {}", shaderSVGFTemporalFilepath.string());
        NEB_ASSERT(m_csATrousSVGF.HasBinary(), "Failed to compile SVGF A-Trous compute shader: {}", shaderSVGFATrousFilepath.string());

        nri::NRIDevice& device = nri::NRIDevice::Get();

        m_svgfTemporalRS = nri::RootSignature(SVGF_TEMPORAL_ROOT_NUM_ROOTS);
        // SVGF_TEMPORAL_ROOT_CONSTANTS = 0,
        // SVGF_TEMPORAL_ROOT_RADIANCE_HISTORY_SRV,
        // SVGF_TEMPORAL_ROOT_DEPTH_CURRENT_SRV,
        // SVGF_TEMPORAL_ROOT_DEPTH_HISTORY_SRV,
        // SVGF_TEMPORAL_ROOT_NORMAL_CURRENT_SRV,
        // SVGF_TEMPORAL_ROOT_NORMAL_HISTORY_SRV,
        // SVGF_TEMPORAL_ROOT_MOMENT_HISTORY_SRV,
        // SVGF_TEMPORAL_ROOT_RADIANCE_CURRENT_UAV,
        // SVGF_TEMPORAL_ROOT_MOMENT_CURRENT_UAV,
        // SVGF_TEMPORAL_ROOT_VARIANCE_UAV,
        // SVGF_TEMPORAL_ROOT_NUM_ROOTS,
        m_svgfTemporalRS.AddParam32BitConstants(SVGF_TEMPORAL_ROOT_CONSTANTS, sizeof(SVGFTemporalConstants) / sizeof(uint32_t), 0);
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_RADIANCE_HISTORY_SRV,   CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 0, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_DEPTH_CURRENT_SRV,      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 1, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_DEPTH_HISTORY_SRV,      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 2, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_NORMAL_CURRENT_SRV,     CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 3, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_NORMAL_HISTORY_SRV,     CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 4, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_MOMENT_HISTORY_SRV,     CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 5, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_RADIANCE_CURRENT_UAV,   CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 1, /*register*/ 0, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_MOMENT_CURRENT_UAV,     CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 1, /*register*/ 1, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_VARIANCE_UAV,           CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 1, /*register*/ 2, /*space*/ 0));
        nri::ThrowIfFalse(m_svgfTemporalRS.Init(&device), "failed to init SVGF temporal compute root sig");
        {
            // A-Trous PSO
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = m_svgfTemporalRS.GetD3D12RootSignature();
            psoDesc.CS = m_csTemporalSVGF.GetBinaryBytecode();
            nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(m_svgfTemporalPSO.ReleaseAndGetAddressOf())));
        }

        m_svgfATrousRS = nri::RootSignature(SVGF_ATROUS_ROOT_NUM_ROOTS);
        m_svgfATrousRS.AddParam32BitConstants(SVGF_ATROUS_ROOT_CONSTANTS, 6, 0);
        m_svgfATrousRS.AddParamDescriptorTable(SVGF_ATROUS_ROOT_SRV, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 4, /*register*/ 0, /*space*/ 0));
        m_svgfATrousRS.AddParamDescriptorTable(SVGF_ATROUS_ROOT_OUTPUT, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 1, /*register*/ 0, /*space*/ 0));
        nri::ThrowIfFalse(m_svgfATrousRS.Init(&device), "failed to init SVGF A-Trous compute root sig");
        {
            // A-Trous PSO
            D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = m_svgfATrousRS.GetD3D12RootSignature();
            psoDesc.CS = m_csATrousSVGF.GetBinaryBytecode();
            nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateComputePipelineState(
                &psoDesc, IID_PPV_ARGS(m_svgfATrousPSO.ReleaseAndGetAddressOf())));
        }
    }

    void SVGFDenoiser::InitSVGFResources()
    {
        NEB_ASSERT(IsInitialized());

        nri::NRIDevice& device = nri::NRIDevice::Get();

        static constexpr auto CreateArrayResource = [](
                                                        UINT width,
                                                        UINT height,
                                                        DXGI_FORMAT format,
                                                        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                                        DXGI_FORMAT clearFormat = DXGI_FORMAT_UNKNOWN,
                                                        UINT arraySize = NumPingPongResources) -> nri::Rc<ID3D12Resource>
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();

            D3D12MA::Allocator* allocator = device.GetResourceAllocator();
            D3D12MA::ALLOCATION_DESC allocDesc = {
                .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                .HeapType = D3D12_HEAP_TYPE_DEFAULT,
            };

            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, arraySize, 1);
            resourceDesc.Flags |= flags;

            Vec4 v = Vec4(0.0f);
            D3D12_CLEAR_VALUE clearValue = { .Format = clearFormat == DXGI_FORMAT_UNKNOWN ? format : clearFormat };

            if (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            {
                clearValue.DepthStencil = {
                    .Depth = 1.0f,
                    .Stencil = 0,
                };
            }
            else
            {
                std::memcpy(clearValue.Color, &v.x, sizeof(clearValue.Color));
            }

            D3D12_CLEAR_VALUE* pClearValue = (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) || (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? &clearValue : nullptr;

            nri::Rc<D3D12MA::Allocation> allocation;
            nri::Rc<ID3D12Resource> resource;
            nri::ThrowIfFailed(allocator->CreateResource(
                                   &allocDesc,
                                   &resourceDesc,
                                   D3D12_RESOURCE_STATE_COMMON, pClearValue,
                                   allocation.GetAddressOf(), IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())),
                "Failed to create array resource");
            return resource;
        };

        UINT width = m_width;
        UINT height = m_height;

        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            m_radiance[i] = CreateArrayResource(width, height, m_radianceFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, DXGI_FORMAT_UNKNOWN, 1);
            NEB_SET_HANDLE_NAME(m_radiance[i], "Radiance array {}", i);
        }
        m_normals = CreateArrayResource(width, height, m_normalFormat, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        NEB_SET_HANDLE_NAME(m_normals, "Normal GBuffer array");

        m_depths = CreateArrayResource(width, height, m_depthResourceFormat, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, m_depthDsvFormat);
        NEB_SET_HANDLE_NAME(m_depths, "Depth stencil buffer array");
        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            m_moments[i] = CreateArrayResource(width, height, m_momentFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, DXGI_FORMAT_UNKNOWN, 1);
            NEB_SET_HANDLE_NAME(m_moments[i], "Moments {}", i);
        }
        m_variance = CreateArrayResource(width, height, m_varianceFormat, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, DXGI_FORMAT_UNKNOWN, 1);
        NEB_SET_HANDLE_NAME(m_variance, "Variance texture");
    }

    void SVGFDenoiser::InitSVGFDescriptors()
    {
        NEB_ASSERT(IsInitialized());
        nri::NRIDevice& device = nri::NRIDevice::Get();

        static constexpr auto CreateSRV = [](DXGI_FORMAT format)
        {
            return D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = format,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
            };
        };
        static constexpr auto CreateUAV = [](DXGI_FORMAT format)
        {
            return D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format = format,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D = { .MipSlice = 0, .PlaneSlice = 0 }
            };
        };
        m_radianceSrvUavHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(NumPingPongResources * 2 /*uav + srv*/);
        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            auto srvDesc = CreateSRV(m_radianceFormat);
            auto uavDesc = CreateUAV(m_radianceFormat);
            device.GetD3D12Device()->CreateShaderResourceView(GetRadianceTexture(i), &srvDesc, m_radianceSrvUavHeap.CpuAt(FirstRadianceSrvIndex + i));
            device.GetD3D12Device()->CreateUnorderedAccessView(GetRadianceTexture(i), nullptr, &uavDesc, m_radianceSrvUavHeap.CpuAt(FirstRadianceUavIndex + i));
        }

        // DepthArray resource represents 2 different depth-stencil buffers as a part of Texture2DArray resource
        // each depth-stencil buffer lives in a corresponding arraySlice index
        auto CreateDepthDSV = [this](uint32_t arraySliceIndex)
        {
            return D3D12_DEPTH_STENCIL_VIEW_DESC{
                .Format = m_depthDsvFormat,
                .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY,
                .Flags = D3D12_DSV_FLAG_NONE,
                .Texture2DArray = { .MipSlice = 0, .FirstArraySlice = arraySliceIndex, .ArraySize = 1 }
            };
        };
        auto CreateDepth_DepthSRV = [this](uint32_t arraySliceIndex)
        {
            return D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = m_depthDepthSrvUavFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = arraySliceIndex, .ArraySize = 1 }
            };
        };
        auto CreateDepth_StencilSRV = [this](uint32_t arraySliceIndex)
        {
            return D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = m_depthStencilSrvUavFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = arraySliceIndex, .ArraySize = 1, .PlaneSlice = 1 }
            };
        };
        m_depthArrayDsvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).AllocateDescriptors(NumPingPongResources);
        m_depthArraySrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(NumPingPongResources * 2 /*Srv + Stencil*/);
        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            auto dsvDesc = CreateDepthDSV(i);
            auto srvDesc = CreateDepth_DepthSRV(i);
            auto srvDescStencil = CreateDepth_StencilSRV(i);
            device.GetD3D12Device()->CreateDepthStencilView(GetDepthArray(), &dsvDesc, m_depthArrayDsvHeap.CpuAt(i));
            device.GetD3D12Device()->CreateShaderResourceView(GetDepthArray(), &srvDesc, m_depthArraySrvHeap.CpuAt(FirstDepthSrvIndex + i));
            device.GetD3D12Device()->CreateShaderResourceView(GetDepthArray(), &srvDescStencil, m_depthArraySrvHeap.CpuAt(FirstStencilSrvIndex + i));
        }

        // Normals
        auto CreateNormalRTV = [this](uint32_t arraySliceIndex)
        {
            return D3D12_RENDER_TARGET_VIEW_DESC{
                .Format = m_normalFormat, // preserve
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray = D3D12_TEX2D_ARRAY_RTV{ .MipSlice = 0, .FirstArraySlice = arraySliceIndex, .ArraySize = 1, .PlaneSlice = 0 }
            };
        };
        auto CreateNormalSRV = [this](uint32_t arraySliceIndex)
        {
            return D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = m_normalFormat,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = arraySliceIndex, .ArraySize = 1 }
            };
        };

        m_normalArrayRtvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptors(NumPingPongResources);
        m_normalArraySrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(NumPingPongResources);
        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            auto rtvDesc = CreateNormalRTV(i);
            auto srvDesc = CreateNormalSRV(i);
            device.GetD3D12Device()->CreateRenderTargetView(GetNormalArray(), &rtvDesc, m_normalArrayRtvHeap.CpuAt(i));
            device.GetD3D12Device()->CreateShaderResourceView(GetNormalArray(), &srvDesc, m_normalArraySrvHeap.CpuAt(i));
        }

        static constexpr auto CreateArraySRV = [](uint32_t arraySliceIndex, DXGI_FORMAT format)
        {
            return D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = format,
                .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2DArray = { .MostDetailedMip = 0, .MipLevels = 1, .FirstArraySlice = arraySliceIndex, .ArraySize = 1 }
            };
        };
        static constexpr auto CreateArrayUAV = [](uint32_t arraySliceIndex, DXGI_FORMAT format)
        {
            return D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format = format,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY,
                .Texture2DArray = { .MipSlice = 0, .FirstArraySlice = arraySliceIndex, .ArraySize = 1, .PlaneSlice = 0 }
            };
        };
        m_momentArraySrvUavHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(NumPingPongResources * 2 /*srv+uav*/);
        for (uint32_t i = 0; i < NumPingPongResources; ++i)
        {
            auto srvDesc = CreateSRV(m_momentFormat);
            auto uavDesc = CreateUAV(m_momentFormat);
            device.GetD3D12Device()->CreateShaderResourceView(GetMomentsTexture(i), &srvDesc, m_momentArraySrvUavHeap.CpuAt(FirstMomentSrvIndex + i));
            device.GetD3D12Device()->CreateUnorderedAccessView(GetMomentsTexture(i), nullptr, &uavDesc, m_momentArraySrvUavHeap.CpuAt(FirstMomentUavIndex + i));
        }

        // 0 srv, 1 uav (only 1 resource, no ping-pong)
        m_varianceSrvUavHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(2); 
        auto srvDesc = CreateSRV(m_varianceFormat);
        auto uavDesc = CreateUAV(m_varianceFormat);
        device.GetD3D12Device()->CreateShaderResourceView(GetVarianceTexture(), &srvDesc, m_varianceSrvUavHeap.CpuAt(0));
        device.GetD3D12Device()->CreateUnorderedAccessView(GetVarianceTexture(), nullptr, &uavDesc, m_varianceSrvUavHeap.CpuAt(1));
    }

} // Neb namespace