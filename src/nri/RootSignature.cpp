#include "RootSignature.h"

#include "common/Assert.h"

namespace Neb::nri
{

    RootSignature& RootSignature::AddParamDescriptorTable(std::span<const D3D12_DESCRIPTOR_RANGE1> descriptorRanges,
        D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1::InitAsDescriptorTable(m_params.emplace_back(),
            static_cast<UINT>(descriptorRanges.size()),
            descriptorRanges.data(), visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParam32BitConstants(UINT num32BitValues, UINT shaderRegister, UINT registerSpace,
        D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1::InitAsConstants(m_params.emplace_back(), num32BitValues, shaderRegister, registerSpace, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamCbv(UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1::InitAsConstantBufferView(m_params.emplace_back(), shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamSrv(UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1::InitAsShaderResourceView(m_params.emplace_back(), shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    RootSignature& RootSignature::AddParamUav(UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
        D3D12_SHADER_VISIBILITY visibility)
    {
        CD3DX12_ROOT_PARAMETER1::InitAsUnorderedAccessView(m_params.emplace_back(), shaderRegister, registerSpace, flags, visibility);
        return *this;
    }

    BOOL RootSignature::Init(nri::NRIDevice* device, D3D12_ROOT_SIGNATURE_FLAGS flags)
    {
        NEB_ASSERT(device, "Invalid device");

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{
            .NumParameters = static_cast<UINT>(m_params.size()),
            .pParameters = m_params.data(),
            .NumStaticSamplers = static_cast<UINT>(m_samplerDescs.size()),
            .pStaticSamplers = m_samplerDescs.data(),
            .Flags = flags,
        };

        nri::D3D12Rc<ID3D10Blob> blob, errorBlob;
        nri::ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, blob.GetAddressOf(), errorBlob.GetAddressOf()));
        nri::ThrowIfFailed(device->GetDevice()->CreateRootSignature(0,
            blob->GetBufferPointer(),
            blob->GetBufferSize(),
            IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())));

        return m_rootSignature != nullptr;
    }

} // Neb::nri namespace