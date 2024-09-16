#pragma once

#include "Device.h"
#include "stdafx.h"

#include <span>
#include <vector>

namespace Neb::nri
{

    class RootSignature
    {
    public:
        RootSignature() = default;

        RootSignature& AddParamDescriptorTable(std::span<const D3D12_DESCRIPTOR_RANGE1> descriptorRanges,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParam32BitConstants(UINT num32BitValues,
            UINT shaderRegister, UINT registerSpace = 0,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamCbv(UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamSrv(UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        RootSignature& AddParamUav(UINT shaderRegister, UINT registerSpace = 0,
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

        inline RootSignature& AddSampler(const D3D12_STATIC_SAMPLER_DESC& samplerDesc)
        {
            m_samplerDescs.push_back(samplerDesc);
            return *this;
        }

        BOOL Init(nri::NRIDevice* device, D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE);

        inline ID3D12RootSignature* GetD3D12RootSignature() const { return m_rootSignature.Get(); }

    private:
        std::vector<CD3DX12_ROOT_PARAMETER1> m_params;
        std::vector<D3D12_STATIC_SAMPLER_DESC> m_samplerDescs;

        Rc<ID3D12RootSignature> m_rootSignature;
    };

} // Neb::nri namespace