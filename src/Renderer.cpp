#include "Renderer.h"

#include "common/Assert.h"
#include "common/Log.h"
#include "nri/imgui/UiContext.h"
#include "nri/nvidia/NvRtxgiNRC.h"
#include "nri/ShaderCompiler.h"
#include "nri/Device.h"
#include "util/ScopedPointer.h"
#include "input/InputManager.h"

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

        // Init fence
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateFence(
            m_fenceValues[m_backbufferIndex],
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));

        InitCommandList();

        m_deferredRenderer.Init(m_swapchain.GetWidth(), m_swapchain.GetHeight(), &m_swapchain);

        nri::UiContext::Get()->Init(nri::UiSpecification{
            .handle = hwnd,
            .device = &nri::NRIDevice::Get(),
            .numInflightFrames = NumInflightFrames,
            .renderTargetFormat = m_swapchain.GetFormat(),
            .depthStencilFormat = m_deferredRenderer.GetDepthStencilBuffer().GetFormat(),
        });

        return TRUE;
    }

    BOOL Renderer::InitSceneContext(Scene* scene)
    {
        NEB_ASSERT(scene, "Invalid scene!");
        m_scene = scene;

        nri::ThrowIfFalse(m_raytracer.Init(&m_swapchain), "Failed to init scene context for ray tracing");

        // use current frame index to submit AS work
        UINT backbufferIndex = m_backbufferIndex;

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        // submit work for building raytracing AS
        nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        nri::ThrowIfFalse(m_raytracer.InitSceneContext(commandList, m_scene), "Failed to init scene context for ray tracing");
        nri::ThrowIfFailed(commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, commandList, m_fence.Get(), m_fenceValues[backbufferIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[backbufferIndex]);

        // wait for work to finish
        // TODO: do in parallel, avoid waiting here
        this->WaitForLastFrame();
        return true;
    }

    void Renderer::RenderSceneDeferred(float timestep)
    {
        NEB_ASSERT(m_scene);

        UINT backbufferIndex = NextFrame();

        // Begin frame (including UI frame)
        nri::UiContext::Get()->BeginFrame();
        m_deferredRenderer.BeginFrame(DeferredRenderer::RenderInfo{
            .scene = m_scene,
            .commandList = GetCommandList(),
            .backbufferIndex = backbufferIndex,
            .frameIndex = GetFrameIndex(),
            .timestep = timestep });

        m_deferredRenderer.SubmitUICommands();
        {

            // Explicitly begin RTXGI frame as a separate queue submit
            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex, [this] {
                static NrcResolveMode mode = NrcResolveMode::AddQueryResultToOutput;
                

                static nrc::FrameSettings NrcFrameSettings;
                // NRC works better when the radiance values it sees internally are in a 'friendly'
                // range for it.
                // Applications often have quite different scales for their radiance units, so
                // we need to be able to scale these units in order to get that nice NRC-friendly range.
                // This value should broadly correspond to the average radiance that you might see
                // in one of your bright scenes (e.g. outdoors in daylight).
                ImGui::DragFloat("Max average radiance", &NrcFrameSettings.maxExpectedAverageRadianceValue, 0.025f, 0.0f, 32.0f);
                // This will prevent NRC from terminating on mirrors - it continue to the next vertex
                ImGui::Checkbox("Skip delta vertices", &NrcFrameSettings.skipDeltaVertices);
                // Knob for the termination heuristic to determine when it terminates the path.
                // The default value should give good quality.  You can decrease the value to
                // bias the algorithm to terminating earlier, trading off quality for performance.
                ImGui::DragFloat("Termination heuristic threshold", &NrcFrameSettings.terminationHeuristicThreshold, 0.01f, 0.0f, 1.0f);
                
                ImGui::Combo("Resolve Mode", (int*)&NrcFrameSettings.resolveMode, nrc::GetImGuiResolveModeComboString());

                // Debugging aid. You can disable the training to 'freeze' the state of the cache.
                ImGui::Checkbox("Train NRC", &NrcFrameSettings.trainTheCache);

                // Proportion of unbiased paths that we should do self-training on
                float proportionUnbiasedToSelfTrain = 1.0f;
                ImGui::DragFloat("Proportion unbiased (self-train)", &NrcFrameSettings.proportionUnbiasedToSelfTrain, 0.01f, 0.0f, 1.0f);

                // Proportion of training paths that should be 'unbiased'
                float proportionUnbiased = 0.0625f;
                ImGui::DragFloat("Proportion unbiased", &NrcFrameSettings.proportionUnbiased, 0.01f, 0.0f, 1.0f);

                // Allows the radiance from self-training to be attenuated or completely disabled
                // to help debugging.
                // If there's an error in the path tracer that breaks energy conservation for example,
                // the self training feedback can sometimes lead to the cache getting brighter
                // and brighter. This control can help debug such issues.
                float selfTrainingAttenuation = 1.0f;
                ImGui::DragFloat("Self-train attenuation", &NrcFrameSettings.selfTrainingAttenuation, 0.025f, 0.0f, 1.0f);

                // This controls how many training iterations are performed each frame,
                // which in turn determines the ideal number of training records that
                // the training/update path tracing pass is expected to generate.
                // Each training batch contains 16K training records derived from path segments
                // in the the NRC update path tracing pass.
                // `ComputeIdealTrainingDimensions` to set a lower resolution in
                // `FrameSettings::usedTrainingDimensions` (and your training/update dispatch).
                ImGui::SliderInt("Training iterations", (int*)&NrcFrameSettings.numTrainingIterations, 1, 32);
                ImGui::DragFloat("LR", &NrcFrameSettings.proportionUnbiased, 0.001f, 0.0f, 0.1f);


                nri::NvRtxgiNRCIntegration* rtxgiIntegration = nri::NvRtxgiNRCIntegration::Get();
                NrcFrameSettings.usedTrainingDimensions = nrc::ComputeIdealTrainingDimensions(rtxgiIntegration->GetContextSettings().frameDimensions, NrcFrameSettings.numTrainingIterations);

                // RTXGI should reset command lists manually
                rtxgiIntegration->BeginFrame(GetCommandList(), NrcFrameSettings);
                rtxgiIntegration->PopulateShaderConstants(&m_deferredRenderer.GetNrcConstants()); // update NrcConstants struct to then be used in constant buffer
            });

            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsGbuffer();
                });

            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsPBRLighting();
                });

            UINT pathtracingFenceValue = ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsGIPathtrace();
                });

            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsHDRTonemapping(GetCommandList());
                });

            // Call explicitly at the very end of a frame
            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this, backbufferIndex]
                {
                    nri::UiContext::Get()->EndFrame();
                    nri::UiContext::Get()->SubmitCommands(backbufferIndex, GetCommandList(), &m_swapchain);
                });

            WaitForFenceValue(pathtracingFenceValue);
            /*ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    float trainingLoss;
                    nri::NvRtxgiNRCIntegration::Get()->QueryAndTrain(GetCommandList(), &trainingLoss);
                    NEB_LOG_INFO("NRC training loss is {}", trainingLoss);
                });*/
        }
        // Explicitly end RTXGI frame context
        nri::NvRtxgiNRCIntegration::Get()->EndFrame(nri::NRIDevice::Get().GetCommandQueue(nri::eCommandContextType_Graphics));
        m_deferredRenderer.EndFrame();

        m_swapchain.Present(FALSE);
    }

    void Renderer::RenderUI(UINT backbufferIndex)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        nri::ThrowIfFailed(m_commandList->Reset(commandAllocator.Get(), nullptr));
        
        nri::ThrowIfFailed(m_commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, m_commandList.Get(), m_fence.Get(), m_fenceValues[backbufferIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[backbufferIndex]);
        ++m_fenceValues[backbufferIndex]; // Increment fence value before proceding to PBR lighting pass
    }

    void Renderer::RenderScene(float timestep)
    {
        //if (!m_scene)
        //{
        //    NEB_LOG_WARN("No scene context provided for Renderer::RenderScene");
        //    return;
        //}

        //// Move to the next frame;
        //UINT backbufferIndex = NextFrame();

        //nri::NRIDevice& device = nri::NRIDevice::Get();
        //nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        //nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        //// Reset with nullptr as initial state, not to be bothered
        //nri::ThrowIfFailed(m_commandList->Reset(commandAllocator.Get(), nullptr));
        //{
        //    nri::UiContext::Get()->BeginFrame();
        //    m_deferredRenderer.SubmitCommandsGbuffer(DeferredRenderer::RenderInfo{
        //        .scene = m_scene,
        //        .commandList = m_commandList.Get(),
        //        .backbufferIndex = backbufferIndex,
        //        .timestep = timestep });
        //    nri::UiContext::Get()->EndFrame();
        //    nri::UiContext::Get()->SubmitCommands(backbufferIndex, m_commandList.Get(), &m_swapchain);
        //}
        //nri::ThrowIfFailed(m_commandList->Close());

        //SubmitCommandList(nri::eCommandContextType_Graphics, m_commandList.Get(), m_fence.Get(), m_fenceValues[backbufferIndex]);
        //commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[backbufferIndex]);

        //m_swapchain.Present(FALSE);
    }

    void Renderer::RenderSceneRaytraced(float timestep)
    {
        // Move to the next frame;
        UINT backbufferIndex = NextFrame();

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        {
            nri::UiContext::Get()->BeginFrame();
            m_raytracer.PopulateCommandLists(commandList, backbufferIndex, timestep);
            nri::UiContext::Get()->EndFrame();
            nri::UiContext::Get()->SubmitCommands(backbufferIndex, commandList, &m_swapchain);
        }
        nri::ThrowIfFailed(commandList->Close());

        SubmitCommandList(nri::eCommandContextType_Graphics, commandList, m_fence.Get(), m_fenceValues[backbufferIndex]);
        commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[backbufferIndex]);

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
            m_raytracer.Resize(width, height); // Finally resize the ray tracing scene
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

    void Renderer::BeginCommandList(nri::ECommandContextType contextType)
    {
        nri::CommandAllocatorPool& commandAllocatorPool = nri::NRIDevice::Get().GetCommandAllocatorPool(contextType);
        m_currentCmdAllocator = commandAllocatorPool.QueryAllocator();
        nri::ThrowIfFailed(m_commandList->Reset(m_currentCmdAllocator.Get(), nullptr));
    }

    UINT Renderer::EndCommandList(nri::ECommandContextType contextType, UINT backbufferIndex)
    {
        NEB_ASSERT(m_currentCmdAllocator != nullptr, "Current command allocator should be valid!");
        nri::ThrowIfFailed(m_commandList->Close());
        {
            SubmitCommandList(contextType, m_commandList.Get(), m_fence.Get(), m_fenceValues[backbufferIndex]);
        }
        nri::NRIDevice::Get().GetCommandAllocatorPool(contextType).DiscardAllocator(m_currentCmdAllocator.Get(), m_fence.Get(), m_fenceValues[backbufferIndex]);
        UINT prevFenceValue = m_fenceValues[backbufferIndex]++; // Increment fence value before proceding to the next command list submition
        m_currentCmdAllocator = nullptr; // reset command allocator
        return prevFenceValue;
    }

    UINT Renderer::NextFrame()
    {
        UINT64 prevFenceValue = m_fenceValues[m_backbufferIndex];

        // only wait for a new frame's fence value
        m_backbufferIndex = m_swapchain.GetCurrentBackbufferIndex();
        this->WaitForFrame(m_backbufferIndex);

        m_fenceValues[m_backbufferIndex] = prevFenceValue + 1;
        ++m_frameIndex; // increment frame index each frame
        return m_backbufferIndex;
    }

    void Renderer::WaitForFrame(UINT backbufferIndex) const
    {
        UINT64 fenceValue = m_fenceValues[backbufferIndex];
        this->WaitForFenceValue(fenceValue);
    }

    void Renderer::WaitForLastFrame() const
    {
        NEB_ASSERT(*std::ranges::max_element(m_fenceValues) == m_fenceValues[m_backbufferIndex], "Fence value we are waiting for needs to be max");
        this->WaitForFrame(m_backbufferIndex);
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

    void Renderer::InitRtxgiContext(UINT width, UINT height, Scene* scene)
    {
        nrc::ContextSettings nrcContextSettings = nrc::ContextSettings();
        nrcContextSettings.learnIrradiance = true;
        nrcContextSettings.includeDirectLighting = true;
        nrcContextSettings.frameDimensions = { (uint32_t)width, (uint32_t)height };

        nrcContextSettings.trainingDimensions = nrc::ComputeIdealTrainingDimensions(nrcContextSettings.frameDimensions, 0);
        nrcContextSettings.maxPathVertices = 8;
        nrcContextSettings.samplesPerPixel = 1;
        
        Vec3 min = scene->SceneBox.min;
        Vec3 max = scene->SceneBox.max;
        nrcContextSettings.sceneBoundsMin = { min.x, min.y, min.z };
        nrcContextSettings.sceneBoundsMax = { max.x, max.y, max.z };
        nrcContextSettings.smallestResolvableFeatureSize = 0.01f;

        nri::NvRtxgiNRCIntegration::Get()->Configure(nrcContextSettings);
    }

} // Neb namespace