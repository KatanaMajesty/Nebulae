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

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = vsBasic.GetBinaryBytecode();
        psoDesc.PS = psBasic.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE; // Lazy to create depth stencil buffer yet
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = {};
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(m_nriManager.GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));

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

        ID3D12CommandAllocator* commandAllocator = m_nriManager.GetCommandAllocator(nri::eCommandContextType_Graphics);
        nri::ThrowIfFailed(commandAllocator->Reset());

        // Reset with nullptr as initial state, not to be bothered
        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator, nullptr));
        {
            // No need to sync backbuffers yet
            // TODO: Revisit this and make it inflight
            ID3D12Resource* backbuffer = m_swapchain.GetCurrentBackbuffer();
            D3D12_RESOURCE_BARRIER backbufferBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
                backbuffer,
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
            m_commandList->ResourceBarrier(1, &backbufferBarrier);

            // Input assembler is such a mess lol, not even a nice-looking enum for primitive topology...
            m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            static const Neb::Vec4 rtvClearColor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            nri::DescriptorAllocation rtvDescriptor = m_swapchain.GetCurrentBackbufferRtv();
            m_commandList->ClearRenderTargetView(rtvDescriptor.DescriptorHandle, &rtvClearColor.x, 0, nullptr);
            m_commandList->OMSetRenderTargets(1, &rtvDescriptor.DescriptorHandle, FALSE, nullptr);

            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // Setup PSO
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
            m_commandList->SetPipelineState(m_pipelineState.Get());
            m_commandList->DrawInstanced(3, 1, 0, 0);

            backbufferBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
                backbuffer,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PRESENT);
            m_commandList->ResourceBarrier(1, &backbufferBarrier);
        }
        nri::ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_commandList.Get() };
        ID3D12CommandQueue* queue = m_nriManager.GetCommandQueue(nri::eCommandContextType_Graphics);
        queue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        m_swapchain.Present(FALSE);

        ID3D12Fence* fence = m_nriManager.GetFence(nri::eCommandContextType_Graphics);
        UINT64& fenceValue = m_nriManager.GetFenceValue(nri::eCommandContextType_Graphics);
        nri::ThrowIfFailed(queue->Signal(fence, ++fenceValue));

        // At the very end, when we are done - wait asset processing for completion
        if (fence->GetCompletedValue() < fenceValue)
        {
            if (!m_fenceEvent)
            {
                m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
                NEB_ASSERT(m_fenceEvent);
            }

            // Wait until the fence is completed.
            nri::ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }

} // Neb namespace