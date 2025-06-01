#include "Renderer.h"

#include "common/Assert.h"
#include "common/Log.h"
#include "nri/imgui/UiContext.h"
#include "nri/ShaderCompiler.h"
#include "nri/Device.h"

// TODO: Remove this when shader library is implemented.
// Currently needed to initialize root signature. Shaders are needed for that and we need assets directory
#include "Nebulae.h"

#include <algorithm>

namespace Neb
{

    Renderer::~Renderer()
    {
        // synchronize all ongoing work, wait for its completion
        this->WaitForLastFrame();
        nri::UiContext::Get()->Shutdown();

        // destroy swapchain here because of singleton destructors being called AFTER device destruction
        m_swapchain.Shutdown();
    }

    BOOL Renderer::Init(HWND hwnd)
    {
        NEB_ASSERT(hwnd != NULL, "Window handle is null");
        m_hwnd = hwnd;

        // Now initialize the swapchain
        if (!m_swapchain.Init(m_hwnd))
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
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateFence(
            m_fenceValues[m_frameIndex],
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));

        InitCommandList();
        InitRootSignatureAndShaders();
        InitPipelineState();
        InitInstanceCb();

        m_deferredRenderer.Init(m_swapchain.GetWidth(), m_swapchain.GetHeight());

        nri::UiContext::Get()->Init(nri::UiSpecification{
            .handle = hwnd,
            .device = &nri::NRIDevice::Get(),
            .numInflightFrames = NumInflightFrames,
            .renderTargetFormat = m_swapchain.GetFormat(),
            .depthStencilFormat = m_depthStencilBuffer.GetFormat(),
        });

        return TRUE;
    }

    BOOL Renderer::InitSceneContext(Scene* scene)
    {
        NEB_ASSERT(scene, "Invalid scene!");
        m_scene = scene;

        nri::ThrowIfFalse(m_raytracer.Init(&m_swapchain), "Failed to init scene context for ray tracing");

        // use current frame index to submit AS work
        UINT frameIndex = m_frameIndex;

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        // submit work for building raytracing AS
        nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        nri::ThrowIfFalse(m_raytracer.InitSceneContext(commandList, m_scene), "Failed to init scene context for ray tracing");
        nri::ThrowIfFailed(commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, commandList, m_fence.Get(), m_fenceValues[frameIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[frameIndex]);

        // wait for work to finish
        // TODO: do in parallel, avoid waiting here
        this->WaitForLastFrame();
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
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        // Reset with nullptr as initial state, not to be bothered
        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator.Get(), nullptr));
        {
            nri::UiContext::Get()->BeginFrame();
            m_deferredRenderer.SubmitCommands(DeferredRenderer::RenderInfo{
                .scene = m_scene,
                .commandList = m_commandList.Get(),
                .frameIndex = frameIndex,
                .timestep = timestep });
            this->PopulateCommandLists(frameIndex, timestep, m_scene);
            nri::UiContext::Get()->EndFrame();
            nri::UiContext::Get()->SubmitCommands(frameIndex, m_commandList.Get(), &m_swapchain);
        }
        nri::ThrowIfFailed(m_commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, m_commandList.Get(), m_fence.Get(), m_fenceValues[frameIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[frameIndex]);

        m_swapchain.Present(FALSE);
    }

    void Renderer::RenderSceneRaytraced(float timestep)
    {
        // Move to the next frame;
        UINT frameIndex = NextFrame();

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        {
            nri::UiContext::Get()->BeginFrame();
            m_raytracer.PopulateCommandLists(commandList, frameIndex, timestep);
            nri::UiContext::Get()->EndFrame();
            nri::UiContext::Get()->SubmitCommands(frameIndex, commandList, &m_swapchain);
        }
        nri::ThrowIfFailed(commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, commandList, m_fence.Get(), m_fenceValues[frameIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[frameIndex]);

        m_swapchain.Present(FALSE);
    }

    void Renderer::Resize(UINT width, UINT height)
    {
        // avoid reallocating everything for no reason
        if (width == m_swapchain.GetWidth() && height == m_swapchain.GetHeight())
            return;

        // Before resizing swapchain wait for all frames to finish rendering
        this->WaitForLastFrame();
        {
            // Handle the return result better
            m_swapchain.Resize(width, height);
            m_deferredRenderer.Resize(width, height);
            m_depthStencilBuffer.Resize(width, height);
            m_raytracer.Resize(width, height); // Finally resize the ray tracing scene
        }
    }

    void Renderer::PopulateCommandLists(UINT frameIndex, float timestep, Scene* scene)
    {
        NEB_ASSERT(m_commandList && scene, "Invalid populate context");
        nri::NRIDevice& device = nri::NRIDevice::Get();

        //ImGui::ShowDemoWindow();

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

            const Mat4& view = scene->Camera.UpdateLookAt();
            const float aspectRatio = m_swapchain.GetWidth() / static_cast<float>(m_swapchain.GetHeight());
            Mat4 projection = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);

            // Bind instance info once, will be updated later
            m_commandList->SetGraphicsRootConstantBufferView(eRendererRoots_InstanceInfo, m_cbInstance.GetGpuVirtualAddress(frameIndex));

            for (nri::StaticMesh& staticMesh : scene->StaticMeshes)
            {
                CbInstanceInfo cbInstanceInfo = CbInstanceInfo{
                    .InstanceToWorld = staticMesh.InstanceToWorld,
                    .ViewProj = view * projection,
                };
                std::memcpy(m_cbInstance.GetMapping<CbInstanceInfo>(frameIndex), &cbInstanceInfo, sizeof(CbInstanceInfo));

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

    void Renderer::SubmitCommandList(nri::ECommandContextType contextType, ID3D12CommandList* commandList, ID3D12Fence* fence, UINT fenceValue)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        ID3D12CommandQueue* queue = device.GetCommandQueue(contextType);
        queue->ExecuteCommandLists(1, &commandList);
        queue->Signal(fence, fenceValue);

        NEB_ASSERT(*std::ranges::max_element(m_fenceValues) == fenceValue, "Fence value we are waiting for needs to be max");
    }

    UINT Renderer::NextFrame()
    {
        UINT64 prevFenceValue = m_fenceValues[m_frameIndex];

        // only wait for a new frame's fence value
        m_frameIndex = m_swapchain.GetCurrentBackbufferIndex();
        this->WaitForFrame(m_frameIndex);

        m_fenceValues[m_frameIndex] = prevFenceValue + 1;
        return m_frameIndex;
    }

    void Renderer::WaitForFrame(UINT frameIndex) const
    {
        UINT64 fenceValue = m_fenceValues[frameIndex];
        this->WaitForFenceValue(fenceValue);
    }

    void Renderer::WaitForLastFrame() const
    {
        NEB_ASSERT(*std::ranges::max_element(m_fenceValues) == m_fenceValues[m_frameIndex], "Fence value we are waiting for needs to be max");
        this->WaitForFrame(m_frameIndex);
    }

    void Renderer::WaitForFenceValue(UINT64 fenceValue) const
    {
        UINT64 completedValue = m_fence->GetCompletedValue();
        if (completedValue < fenceValue)
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
        m_commandList.Reset();
        nri::Rc<ID3D12GraphicsCommandList> commandList;
        {
            nri::ThrowIfFailed(device.GetD3D12Device()->CreateCommandList1(0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                D3D12_COMMAND_LIST_FLAG_NONE,
                IID_PPV_ARGS(commandList.ReleaseAndGetAddressOf())));
        }
        nri::ThrowIfFailed(commandList.As(&m_commandList));
    }

    void Renderer::InitRootSignatureAndShaders()
    {
        // Shader and root-signature related stuff
        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::string shaderFilepath = (shaderDir / "Basic.hlsl").string();

        m_vsBasic = nri::ShaderCompiler::Get()->CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex),
            nri::eShaderCompilationFlag_None);

        m_psBasic = nri::ShaderCompiler::Get()->CompileShader(
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
        nri::ThrowIfFailed(device.GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    }

    void Renderer::InitInstanceCb()
    {
        nri::ThrowIfFalse(m_cbInstance.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbInstanceInfo) }));
    }

} // Neb namespace