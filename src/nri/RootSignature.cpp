#include "RootSignature.h"

#include "common/Assert.h"

#include <algorithm>
#include <ranges>

namespace Neb::nri
{
    
    RootSignature RootSignature::Empty(NRIDevice* device, D3D12_ROOT_SIGNATURE_FLAGS flags)
    {
        RootSignature rs = RootSignature();
        nri::ThrowIfFalse(rs.Init(device, flags), "failed to initialize empty root signature, is device context correct?");

        return rs;
    }

    RootSignature::RootSignature(UINT numRootParams, UINT numStaticSamplers)
    {
        m_params.resize(numRootParams);
        m_samplerDescs.resize(numStaticSamplers);
    }

    RootSignature& RootSignature::AddParamDescriptorTable(UINT rootParamIndex, std::span<const D3D12_DESCRIPTOR_RANGE1> descriptorRanges,
        D3D12_SHADER_VISIBILITY visibility)
    {
        NEB_LOG_WARN_IF(m_params.at(rootParamIndex).has_value(), "Root signature -> param at {} (Descriptor table) was overridden", rootParamIndex);
        m_params.at(rootParamIndex) = CD3DX12_ROOT_PARAMETER1();
        CD3DX12_ROOT_PARAMETER1& param = m_params.at(rootParamIndex).value();
        CD3DX12_ROOT_PARAMETER1::InitAsDescriptorTable(param,
            static_cast<UINT>(descriptorRanges.size()),
            descriptorRanges.data(), visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParam32BitConstants(UINT rootParamIndex, UINT num32BitValues, UINT shaderRegister, UINT registerSpace,
        D3D12_SHADER_VISIBILITY visibility)
    {
        NEB_LOG_WARN_IF(m_params.at(rootParamIndex).has_value(), "Root signature -> param at {} (32BIT) was overridden", rootParamIndex);
        m_params.at(rootParamIndex) = CD3DX12_ROOT_PARAMETER1();
        CD3DX12_ROOT_PARAMETER1& param = m_params.at(rootParamIndex).value();
        CD3DX12_ROOT_PARAMETER1::InitAsConstants(param, num32BitValues, shaderRegister, registerSpace, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamCbv(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        NEB_LOG_WARN_IF(m_params.at(rootParamIndex).has_value(), "Root signature -> param at {} (CBV) was overridden", rootParamIndex);
        m_params.at(rootParamIndex) = CD3DX12_ROOT_PARAMETER1();
        CD3DX12_ROOT_PARAMETER1& param = m_params.at(rootParamIndex).value();
        CD3DX12_ROOT_PARAMETER1::InitAsConstantBufferView(param, shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamSrv(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        NEB_LOG_WARN_IF(m_params.at(rootParamIndex).has_value(), "Root signature -> param at {} (SRV) was overridden", rootParamIndex);
        m_params.at(rootParamIndex) = CD3DX12_ROOT_PARAMETER1();
        CD3DX12_ROOT_PARAMETER1& param = m_params.at(rootParamIndex).value();
        CD3DX12_ROOT_PARAMETER1::InitAsShaderResourceView(param, shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamUav(UINT rootParamIndex, UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        NEB_LOG_WARN_IF(m_params.at(rootParamIndex).has_value(), "Root signature -> param at {} (UAV) was overridden", rootParamIndex);
        m_params.at(rootParamIndex) = CD3DX12_ROOT_PARAMETER1();
        CD3DX12_ROOT_PARAMETER1& param = m_params.at(rootParamIndex).value();
        CD3DX12_ROOT_PARAMETER1::InitAsUnorderedAccessView(param, shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddStaticSampler(UINT staticSamplerIndex, const D3D12_STATIC_SAMPLER_DESC& samplerDesc)
    {
        if (m_samplerDescs.at(staticSamplerIndex).has_value())
        {
            NEB_LOG_WARN("Root signature {} -> sampler desc at {} was overridden", "unnamed", staticSamplerIndex);
        }
        m_samplerDescs.at(staticSamplerIndex) = samplerDesc;
        return *this;
    }

    BOOL RootSignature::Init(nri::NRIDevice* device, D3D12_ROOT_SIGNATURE_FLAGS flags)
    {
        NEB_ASSERT(device, "Invalid device");

        std::vector<CD3DX12_ROOT_PARAMETER1> params(m_params.size());
        for (UINT rootIndex = 0; rootIndex < m_params.size(); ++rootIndex)
        {
            const ParameterEntry& entry = m_params.at(rootIndex);
            if (!entry.has_value())
            {
                NEB_LOG_ERROR("Parameter at root index {} was not set!", rootIndex);
                return false;
            }
            
            params.at(rootIndex) = entry.value();
        }

        std::vector<D3D12_STATIC_SAMPLER_DESC> samplerDescs(m_samplerDescs.size());
        for (UINT samplerIndex = 0; samplerIndex < m_samplerDescs.size(); ++samplerIndex)
        {
            const StSamplerEntry& entry = m_samplerDescs.at(samplerIndex);
            if (!entry.has_value())
            {
                NEB_LOG_ERROR("Static sampler at index {} was not set!", samplerIndex);
                return false;
            }

            samplerDescs.at(samplerIndex) = entry.value();
        }

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{
            .NumParameters = static_cast<UINT>(m_params.size()),
            .pParameters = params.data(),
            .NumStaticSamplers = static_cast<UINT>(m_samplerDescs.size()),
            .pStaticSamplers = samplerDescs.data(),
            .Flags = flags,
        };

        nri::D3D12Rc<ID3D10Blob> blob, errorBlob;
        nri::ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, blob.GetAddressOf(), errorBlob.GetAddressOf()));
        nri::ThrowIfFailed(device->GetDevice()->CreateRootSignature(0,
            blob->GetBufferPointer(),
            blob->GetBufferSize(),
            IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));

        return this->IsValid();
    }

} // Neb::nri namespace