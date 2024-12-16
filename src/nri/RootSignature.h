#pragma once

#include "Device.h"
#include "stdafx.h"

#include <span>
#include <vector>
#include <optional>

namespace Neb::nri
{

    class RootSignature
    {
    public:
        RootSignature(UINT numRootParams = 0, UINT numStaticSamplers = 0);

        RootSignature& AddParamDescriptorTable(UINT rootParamIndex, std::span<const D3D12_DESCRIPTOR_RANGE1> descriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParam32BitConstants(UINT rootParamIndex, UINT num32BitValues,
            UINT shaderRegister, UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamCbv(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamSrv(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamUav(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddStaticSampler(UINT staticSamplerIndex, const D3D12_STATIC_SAMPLER_DESC& samplerDesc);

        BOOL Init(nri::NRIDevice* device, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

        inline ID3D12RootSignature* GetD3D12RootSignature() const { return m_rootSignature.Get(); }

    private:
        using ParameterEntry = std::optional<CD3DX12_ROOT_PARAMETER1>;
        using StSamplerEntry = std::optional<D3D12_STATIC_SAMPLER_DESC>;

        std::vector<ParameterEntry> m_params;
        std::vector<StSamplerEntry> m_samplerDescs;

        Rc<ID3D12RootSignature> m_rootSignature;
    };

} // Neb::nri namespace