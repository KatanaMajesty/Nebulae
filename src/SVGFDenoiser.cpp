#include "SVGFDenoiser.h"

#include "nri/Device.h"
#include "nri/ShaderCompiler.h"
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
        return true;
    }

    bool SVGFDenoiser::Resize(UINT width, UINT height)
    {
        NEB_ASSERT(IsInitialized());
        m_width = width;
        m_height = height;

        InitSVGFResources();
        return false;
    }

    void SVGFDenoiser::SubmitTemporalAccumulation(ID3D12GraphicsCommandList4* commandList)
    {
        NEB_ASSERT(IsInitialized());

        UINT width = m_width;
        UINT height = m_height;
        {
            /*commandList->SetPipelineState(m_svgfTemporalPSO.Get());
            commandList->SetComputeRootSignature(m_svgfTemporalRS.GetD3D12RootSignature());

            uint32_t resolution[2] = { width, height };
            commandList->SetComputeRootConstantBufferView(RADIANCE_RESOLVE_ROOT_NRC_CONSTANTS, m_nrcConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));
            commandList->SetComputeRoot32BitConstants(RADIANCE_RESOLVE_ROOT_SCREEN_CONSTANTS, 2, resolution, 0);
            commandList->SetComputeRootDescriptorTable(RADIANCE_RESOLVE_ROOT_NRC_BUFFERS, m_radianceResolveNrcSrvHeap.GpuAddress);
            commandList->SetComputeRootDescriptorTable(RADIANCE_RESOLVE_ROOT_HDR_OUTPUT, m_pbrSrvUavHeap.GpuAt(HDR_UAV_INDEX));

            commandList->Dispatch(width / 8, height / 8, 1);*/
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
        m_svgfTemporalRS.AddParam32BitConstants(SVGF_TEMPORAL_ROOT_CONSTANTS, 5, 0);
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_SRV, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 6, /*register*/ 0, /*space*/ 0));
        m_svgfTemporalRS.AddParamDescriptorTable(SVGF_TEMPORAL_ROOT_UAV, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 3, /*register*/ 0, /*space*/ 0));
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
    }

} // Neb namespace