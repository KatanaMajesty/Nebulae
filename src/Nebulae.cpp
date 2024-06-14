#include "Nebulae.h"

#include "common/Defines.h"
#include "common/Log.h"

namespace Neb
{

    BOOL Nebulae::Init(const AppSpec& appSpec)
    {
        // Firstly initialize rendering interface manager
        // it is now singleton, annoying to manage it all the time
        nri::Manager& nriManager = nri::Manager::Get();

        // Now initialize the swapchain
        if (!m_swapchain.Init(appSpec.Handle))
        {
            NEB_ASSERT(false); // failed to initialize the swapchain
            return FALSE;
        }

        if (!m_depthStencilBuffer.Init(m_swapchain.GetWidth(), m_swapchain.GetHeight()))
        {
            NEB_ASSERT(false); // failed to initialize depth-stencil buffer
            return FALSE;
        }

        // Command related stuff
        nri::ThrowIfFailed(nriManager.GetDevice()->CreateCommandList1(0, 
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

        std::vector<CD3DX12_ROOT_PARAMETER1> rootParams;
        CD3DX12_ROOT_PARAMETER1& cbInstanceInfoRootParam = rootParams.emplace_back();
        cbInstanceInfoRootParam.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_ROOT_PARAMETER1& materialTexturesRootParam = rootParams.emplace_back();
        D3D12_DESCRIPTOR_RANGE1 materialTexturesRange 
            = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nri::eMaterialTextureType_NumTypes, 0, 0);
        materialTexturesRootParam.InitAsDescriptorTable(1, &materialTexturesRange, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC staticSampler = CD3DX12_STATIC_SAMPLER_DESC(0);

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSignatureDesc.Desc_1_1 = D3D12_ROOT_SIGNATURE_DESC1{
            .NumParameters = static_cast<UINT>(rootParams.size()),
            .pParameters = rootParams.data(),
            .NumStaticSamplers = 1,
            .pStaticSamplers = &staticSampler,
            .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
        };

        nri::D3D12Rc<ID3D10Blob> blob, errorBlob;
        nri::ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, blob.GetAddressOf(), errorBlob.GetAddressOf()));
        nri::ThrowIfFailed(nriManager.GetDevice()->CreateRootSignature(0, 
            blob->GetBufferPointer(), 
            blob->GetBufferSize(), 
            IID_PPV_ARGS(m_rootSignature.ReleaseAndGetAddressOf())
        ));

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = vsBasic.GetBinaryBytecode();
        psoDesc.PS = psBasic.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // glTF 2.0 spec: the winding order triangle faces is CCW
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); // Lazy to create depth stencil buffer yet
        //psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = D3D12_INPUT_LAYOUT_DESC{ 
            .pInputElementDescs = nri::StaticMeshInputLayout.data(), 
            .NumElements = nri::StaticMeshInputLayout.size(),
        };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(nriManager.GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));

        m_sceneImporter.Clear();

        InitInstanceInfoCb();

        // At the very end begin the time watch
        m_timeWatch.Begin();
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
        if (!scene)
        {
            return;
        }

        int64_t elapsedSeconds = m_timeWatch.Elapsed();
        int64_t timestepMillis = elapsedSeconds - m_lastFrameSeconds;
        m_lastFrameSeconds = elapsedSeconds;
        const float timestep = timestepMillis * 0.001f;

        nri::Manager& nriManager = nri::Manager::Get();

        ID3D12CommandAllocator* commandAllocator = nriManager.GetCommandAllocator(nri::eCommandContextType_Graphics);
        nri::ThrowIfFailed(commandAllocator->Reset());

        // Reset with nullptr as initial state, not to be bothered
        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator, nullptr));
        {
            std::array shaderVisibleHeaps = {
                nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap(),
                nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).GetHeap(),
            };
            m_commandList->SetDescriptorHeaps(static_cast<UINT>(shaderVisibleHeaps.size()), shaderVisibleHeaps.data());
    
            // No need to sync backbuffers yet
            // TODO: Revisit this and make it inflight

            ID3D12Resource* backbuffer = m_swapchain.GetCurrentBackbuffer();
            std::array<D3D12_RESOURCE_BARRIER, 2> barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
                CD3DX12_RESOURCE_BARRIER::Transition(
                    m_depthStencilBuffer.GetBufferResource(), 
                    D3D12_RESOURCE_STATE_COMMON, 
                    D3D12_RESOURCE_STATE_DEPTH_WRITE),
            };

            m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            static const Neb::Vec4 rtvClearColor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            const nri::DescriptorAllocation& rtvDescriptor = m_swapchain.GetCurrentBackbufferRtv();
            m_commandList->ClearRenderTargetView(rtvDescriptor.DescriptorHandle, &rtvClearColor.x, 0, nullptr);

            const nri::DescriptorAllocation& dsvDescriptor = m_depthStencilBuffer.GetDSV();
            m_commandList->ClearDepthStencilView(dsvDescriptor.DescriptorHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            m_commandList->OMSetRenderTargets(1, &rtvDescriptor.DescriptorHandle, FALSE, &m_depthStencilBuffer.GetDSV().DescriptorHandle);

            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // Setup PSO
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
            m_commandList->SetPipelineState(m_pipelineState.Get());

            static float rotationAngleY = 0.0f;
            rotationAngleY += 30.0f * timestep;

            const Vec3 rotationAngles = Vec3(ToRadians(90.0f), ToRadians(rotationAngleY), ToRadians(0.0f));

            Mat4 translation = Mat4::CreateTranslation(Vec3(0.0f, 0.0f, -2.0f));
            Mat4 rotation = Mat4::CreateFromYawPitchRoll(rotationAngles);
            Mat4 scale = Mat4::CreateScale(Vec3(1.0f));

            const float aspectRatio = m_swapchain.GetWidth() / static_cast<float>(m_swapchain.GetHeight());
            CbInstanceInfo cbInstanceInfo = CbInstanceInfo{
                .InstanceToWorld = scale * rotation * translation,
                .Projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f),
            };

            std::memcpy(m_cbInstanceInfoBufferMapping, &cbInstanceInfo, sizeof(CbInstanceInfo));
            m_commandList->SetGraphicsRootConstantBufferView(0, m_cbInstanceInfoBuffer->GetGPUVirtualAddress());

            for (nri::StaticMesh& staticMesh : scene->StaticMeshes)
            {
                NEB_ASSERT(staticMesh.Submeshes.size() == staticMesh.SubmeshMaterials.size());

                const size_t numSubmeshes = staticMesh.Submeshes.size();
                for (size_t i = 0; i < numSubmeshes; ++i)
                {
                    nri::StaticSubmesh& submesh = staticMesh.Submeshes[i];
                    nri::Material& material = staticMesh.SubmeshMaterials[i];

                    m_commandList->SetGraphicsRootDescriptorTable(1, material.SrvRange.GPUBeginHandle);

                    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_commandList->IASetVertexBuffers(0, nri::eAttributeType_NumTypes, submesh.AttributeViews.data());
                    m_commandList->IASetIndexBuffer(&submesh.IBView);
                    m_commandList->DrawIndexedInstanced(submesh.NumIndices, 1, 0, 0, 0);
                }
            }

            barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
                CD3DX12_RESOURCE_BARRIER::Transition(
                    m_depthStencilBuffer.GetBufferResource(), 
                    D3D12_RESOURCE_STATE_DEPTH_WRITE, 
                    D3D12_RESOURCE_STATE_COMMON),
            };
            m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
        nri::ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_commandList.Get() };
        ID3D12CommandQueue* queue = nriManager.GetCommandQueue(nri::eCommandContextType_Graphics);
        queue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);

        m_swapchain.Present(FALSE);

        ID3D12Fence* fence = nriManager.GetFence(nri::eCommandContextType_Graphics);
        UINT64& fenceValue = nriManager.GetFenceValue(nri::eCommandContextType_Graphics);
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

    void Nebulae::InitInstanceInfoCb()
    {
        nri::Manager& nriManager = nri::Manager::Get();

        D3D12_RESOURCE_DESC cbInstanceInfoResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{
            .SizeInBytes = sizeof(CbInstanceInfo),
            .Alignment = 0
            });
        D3D12MA::Allocator* allocator = nriManager.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC cbInstanceInfoAllocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_UPLOAD, // It alright to use upload heap for small-sized resources (i guess?)
        };
        nri::D3D12Rc<D3D12MA::Allocation> allocation;
        nri::ThrowIfFailed(nriManager.GetResourceAllocator()->CreateResource(
            &cbInstanceInfoAllocDesc,
            &cbInstanceInfoResourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            allocation.GetAddressOf(),
            IID_PPV_ARGS(m_cbInstanceInfoBuffer.ReleaseAndGetAddressOf())
        ));

        void* mapping;
        nri::ThrowIfFailed(m_cbInstanceInfoBuffer->Map(0, nullptr, &mapping));
        NEB_ASSERT(mapping);
        m_cbInstanceInfoBufferMapping = reinterpret_cast<CbInstanceInfo*>(mapping);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbInstanceInfoDesc = {
            .BufferLocation = m_cbInstanceInfoBuffer->GetGPUVirtualAddress(),
            .SizeInBytes = sizeof(CbInstanceInfo)
        };
        m_cbInstanceInfoDescriptor = nriManager.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptor();
        nriManager.GetDevice()->CreateConstantBufferView(&cbInstanceInfoDesc, m_cbInstanceInfoDescriptor.DescriptorHandle);
    }

} // Neb namespace