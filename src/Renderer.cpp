#include "Renderer.h"

#include "common/Assert.h"
#include "common/Log.h"
#include "nri/Device.h"

// TODO: Remove this when shader library is implemented.
// Currently needed to initialize root signature. Shaders are needed for that and we need assets directory
#include "Nebulae.h"

namespace Neb
{

    BOOL Renderer::Init(HWND hwnd)
    {
        NEB_ASSERT(hwnd != NULL, "Window handle is null");

        // Now initialize the swapchain
        if (!m_swapchain.Init(hwnd))
        {
            NEB_LOG_ERROR("Failed to initialize swapchain");
            return FALSE;
        }

        if (!m_depthStencilBuffer.Init(m_swapchain.GetWidth(), m_swapchain.GetHeight()))
        {
            NEB_LOG_ERROR("Failed to initialize depth-stencil buffer");
            return FALSE;
        }

        // Init fence
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetDevice()->CreateFence(
            m_fenceValues[m_frameIndex],
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));

        InitCommandList();
        InitRootSignatureAndShaders();
        InitPipelineState();
        InitInstanceCb();
        return TRUE;
    }

    BOOL Renderer::InitScene(Scene* scene)
    {
        NEB_ASSERT(scene, "Invalid scene!");
        m_scene = scene;

        // Validate if swapchains extents are valid here
        if (!m_rtScene.InitForScene(&m_swapchain, m_scene))
        {
            return false;
        }
        return true;
    }

    void Renderer::RenderScene(float timestep)
    {
        if (!m_scene)
        {
            NEB_LOG_WARN("No scene context provided for Renderer::RenderScene");
            return;
        }

        // Move to the next frame;
        UINT frameIndex = NextFrame();

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::D3D12Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        // Reset with nullptr as initial state, not to be bothered
        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator.Get(), nullptr));
        {
            PopulateCommandLists(frameIndex, timestep, m_scene);
        }
        nri::ThrowIfFailed(m_commandList->Close());

        ID3D12CommandList* pCommandLists[] = { m_commandList.Get() };
        ID3D12CommandQueue* queue = device.GetCommandQueue(nri::eCommandContextType_Graphics);
        queue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);
        queue->Signal(m_fence.Get(), m_fenceValues[frameIndex]);

        m_swapchain.Present(FALSE);

        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[frameIndex]);
    }

    void Renderer::RenderSceneRaytraced(float timestep)
    {
        // Move to the next frame;
        UINT frameIndex = NextFrame();

        m_rtScene.Render(frameIndex, m_fence.Get(), m_fenceValues[frameIndex]);
    }

    void Renderer::Resize(UINT width, UINT height)
    {
        // avoid reallocating everything for no reason
        if (width == m_swapchain.GetWidth() && height == m_swapchain.GetHeight())
            return;

        // Before resizing swapchain wait for all frames to finish rendering
        WaitForAllFrames();
        m_rtScene.WaitForGpuContext(); // Wait for ray tracing to finish as it is executed on the same command queue
        {
            // Handle the return result better
            m_swapchain.Resize(width, height);
            m_depthStencilBuffer.Resize(width, height);
            m_rtScene.Resize(width, height); // Finally resize the ray tracing scene
        }
    }

    void Renderer::Shutdown()
    {
        WaitForAllFrames();
        m_rtScene.WaitForGpuContext();

        m_swapchain = nri::Swapchain();
    }

    void Renderer::PopulateCommandLists(UINT frameIndex, float timestep, Scene* scene)
    {
        NEB_ASSERT(m_commandList && scene, "Invalid populate context");
        nri::NRIDevice& device = nri::NRIDevice::Get();

        {
            std::array shaderVisibleHeaps = {
                device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap(),
                device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).GetHeap(),
            };
            m_commandList->SetDescriptorHeaps(static_cast<UINT>(shaderVisibleHeaps.size()), shaderVisibleHeaps.data());

            ID3D12Resource* backbuffer = m_swapchain.GetCurrentBackbuffer();
            {
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
                    CD3DX12_RESOURCE_BARRIER::Transition(
                        m_depthStencilBuffer.GetBufferResource(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_DEPTH_WRITE),
                };
                m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }

            static const Neb::Vec4 rtvClearColor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = m_swapchain.GetCurrentBackbufferRtvHandle();
            m_commandList->ClearRenderTargetView(rtvDescriptor, &rtvClearColor.x, 0, nullptr);

            const nri::DescriptorHeapAllocation& dsvDescriptor = m_depthStencilBuffer.GetDSV();
            m_commandList->ClearDepthStencilView(dsvDescriptor.CpuAt(0), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            m_commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &m_depthStencilBuffer.GetDSV().CpuAddress);

            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, m_swapchain.GetWidth(), m_swapchain.GetHeight());
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // Setup PSO
            m_commandList->SetGraphicsRootSignature(m_rootSignature.GetD3D12RootSignature());
            m_commandList->SetPipelineState(m_pipelineState.Get());

            static float rotationAngleY = 0.0f;
            // rotationAngleY += 30.0f * timestep;

            const Vec3 rotationAngles = Vec3(ToRadians(90.0f), ToRadians(rotationAngleY), ToRadians(0.0f));

            Mat4 translation = Mat4::CreateTranslation(Vec3(0.0f, 0.0f, 0.0f));
            Mat4 rotation = Mat4::CreateFromYawPitchRoll(rotationAngles);
            Mat4 scale = Mat4::CreateScale(Vec3(1.0f));

            const Mat4& view = scene->Camera.UpdateLookAt();
            const float aspectRatio = m_swapchain.GetWidth() / static_cast<float>(m_swapchain.GetHeight());
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);

            CbInstanceInfo cbInstanceInfo = CbInstanceInfo{
                .InstanceToWorld = scale * rotation * translation,
                .ViewProj = view * projection,
            };

            std::memcpy(m_cbInstance.GetMapping<CbInstanceInfo>(frameIndex), &cbInstanceInfo, sizeof(CbInstanceInfo));
            m_commandList->SetGraphicsRootConstantBufferView(eRendererRoots_InstanceInfo, m_cbInstance.GetGpuVirtualAddress(frameIndex));

            for (nri::StaticMesh& staticMesh : scene->StaticMeshes)
            {
                const size_t numSubmeshes = staticMesh.Submeshes.size();
                NEB_ASSERT(numSubmeshes == staticMesh.SubmeshMaterials.size(),
                    "Static mesh is invalid. It has {} submeshes while only {} materials",
                    numSubmeshes, staticMesh.SubmeshMaterials.size());

                for (size_t i = 0; i < numSubmeshes; ++i)
                {
                    nri::StaticSubmesh& submesh = staticMesh.Submeshes[i];
                    nri::Material& material = staticMesh.SubmeshMaterials[i];

                    m_commandList->SetGraphicsRootDescriptorTable(eRendererRoots_MaterialTextures, material.SrvRange.GpuAddress);

                    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    m_commandList->IASetVertexBuffers(0, nri::eAttributeType_NumTypes, submesh.AttributeViews.data());
                    m_commandList->IASetIndexBuffer(&submesh.IBView);
                    m_commandList->DrawIndexedInstanced(submesh.NumIndices, 1, 0, 0, 0);
                }
            }

            // Do transition in a separate scope for better readibility
            {
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(
                        m_depthStencilBuffer.GetBufferResource(),
                        D3D12_RESOURCE_STATE_DEPTH_WRITE,
                        D3D12_RESOURCE_STATE_COMMON),
                };
                m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }
            
        }
    }

    UINT Renderer::NextFrame()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
        m_frameIndex = m_swapchain.GetCurrentBackbufferIndex();
        {
            WaitForAllFrames();
        }
        m_fenceValues[m_frameIndex] = currentFenceValue + 1;
        return m_frameIndex;
    }

    void Renderer::WaitForAllFrames()
    {
        UINT64 fenceValue = m_fenceValues[m_frameIndex];
        if (m_fence->GetCompletedValue() < fenceValue)
        {
            HANDLE fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            NEB_ASSERT(fenceEvent, "Failed to create HANDLE for event");

            // Wait until the fence is completed.
            nri::ThrowIfFailed(m_fence->SetEventOnCompletion(fenceValue, fenceEvent));
            WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void Renderer::InitCommandList()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        // Command related stuff
        nri::ThrowIfFailed(device.GetDevice()->CreateCommandList1(0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            D3D12_COMMAND_LIST_FLAG_NONE,
            IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));
    }

    void Renderer::InitRootSignatureAndShaders()
    {
        // Shader and root-signature related stuff
        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::string shaderFilepath = (shaderDir / "Basic.hlsl").string();

        m_vsBasic = m_shaderCompiler.CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex),
            nri::eShaderCompilationFlag_None);

        m_psBasic = m_shaderCompiler.CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel),
            nri::eShaderCompilationFlag_None);

        D3D12_DESCRIPTOR_RANGE1 materialTexturesRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nri::eMaterialTextureType_NumTypes, 0, 0);
        m_rootSignature = nri::RootSignature(eRendererRoots_NumRoots, 1)
                              .AddParamCbv(eRendererRoots_InstanceInfo, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX)
                              .AddParamDescriptorTable(eRendererRoots_MaterialTextures, std::array{ materialTexturesRange }, D3D12_SHADER_VISIBILITY_PIXEL)
                              .AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));
        
        nri::ThrowIfFalse(m_rootSignature.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT));
    }

    void Renderer::InitPipelineState()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_rootSignature.GetD3D12RootSignature();
        psoDesc.VS = m_vsBasic.GetBinaryBytecode();
        psoDesc.PS = m_psBasic.GetBinaryBytecode();
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
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(device.GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    }

    void Renderer::InitInstanceCb()
    {
        nri::ThrowIfFalse(m_cbInstance.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbInstanceInfo) }));
    }

} // Neb namespace