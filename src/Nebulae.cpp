#include "Nebulae.h"

#include "common/Defines.h"

namespace Neb
{

    BOOL Nebulae::Init(const AppSpec& appSpec)
    {
        // Firstly initialize rendering interface manager
        m_nriManager.Init();

        // Now initialize the swapchain
        if (!m_swapchain.Init(appSpec.Handle, &m_nriManager))
        {
            NEB_ASSERT(false); // failed to initialize the swapchain
            return FALSE;
        }

        // Command related stuff
        nri::ThrowIfFailed(m_nriManager.GetDevice()->CreateCommandList1(0, 
            D3D12_COMMAND_LIST_TYPE_DIRECT, 
            D3D12_COMMAND_LIST_FLAG_NONE, 
            IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())
        ));

        // Shader and pipeline related stuff
        const std::filesystem::path basicFilepath = appSpec.AssetsDirectory / "shaders" / "Basic.hlsl";
        nri::Shader vsBasic = m_shaderCompiler.CompileShader(
            basicFilepath.string(),
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex), 
            nri::eShaderCompilationFlag_None
        );

        nri::Shader psBasic = m_shaderCompiler.CompileShader(
            basicFilepath.string(),
            nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel),
            nri::eShaderCompilationFlag_None
        );

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{
            .NumParameters = 0,
            .pParameters = nullptr,
            .NumStaticSamplers = 0,
            .pStaticSamplers = nullptr,
            .Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE,
        };

        nri::D3D12Rc<ID3D10Blob> blob, errorBlob;
        nri::ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, blob.GetAddressOf(), errorBlob.GetAddressOf()));
        nri::ThrowIfFailed(m_nriManager.GetDevice()->CreateRootSignature(0, 
            blob->GetBufferPointer(), 
            blob->GetBufferSize(), 
            IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())
        ));

        D3D12_GRAPHICS_PIPELINE_STATE_DESC stateDesc = {};
        stateDesc.pRootSignature = m_rootSignature.Get();
        stateDesc.VS = vsBasic.GetBinaryBytecode();
        stateDesc.PS = psBasic.GetBinaryBytecode();
        stateDesc.DS = CD3DX12_SHADER_BYTECODE();
        stateDesc.HS = CD3DX12_SHADER_BYTECODE();
        stateDesc.GS = CD3DX12_SHADER_BYTECODE();
        stateDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        stateDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        stateDesc.DepthStencilState.DepthEnable = FALSE; // Lazy to create depth stencil buffer yet
        stateDesc.InputLayout = {};
        stateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        stateDesc.NumRenderTargets = 1;
        stateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        stateDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        stateDesc.SampleDesc = { 1, 0 };
        stateDesc.NodeMask = 0;
        stateDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(m_nriManager.GetDevice()->CreateGraphicsPipelineState(&stateDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));

        m_sceneImporter = GLTFSceneImporter(&m_nriManager);
        return TRUE;
    }

    void Nebulae::Resize(UINT width, UINT height)
    {
        // Handle the return result better
        m_swapchain.Resize(width, height);
    }

    void Nebulae::Render(GLTFScene* scene)
    {
        NEB_ASSERT(m_commandList);

        // Reset with nullptr as initial state, not to be bothered
        nri::ThrowIfFailed(m_commandList->Reset(m_nriManager.GetCommandAllocator(nri::eCommandContextType_Graphics), nullptr));
        {
            
        }
        nri::ThrowIfFailed(m_commandList->Close());
    }

} // Neb namespace