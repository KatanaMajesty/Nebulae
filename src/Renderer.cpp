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
            .depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT,
        });

        return TRUE;
    }

    BOOL Renderer::InitSceneContext(Scene* scene)
    {
        NEB_ASSERT(scene, "Invalid scene!");
        m_scene = scene;

        //nri::ThrowIfFalse(m_raytracer.Init(&m_swapchain), "Failed to init scene context for ray tracing");

        // use current frame index to submit AS work
        UINT backbufferIndex = m_backbufferIndex;

        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        // submit work for building raytracing AS
        nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        //nri::ThrowIfFalse(m_raytracer.InitSceneContext(commandList, m_scene), "Failed to init scene context for ray tracing");
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
        m_deferredRenderer.SubmitUICommands();
        {
            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this, backbufferIndex, timestep]
                {
                    m_deferredRenderer.BeginFrame(DeferredRenderer::RenderInfo{
                        .scene = m_scene,
                        .commandList = GetCommandList(),
                        .backbufferIndex = backbufferIndex,
                        .frameIndex = GetFrameIndex(),
                        .timestep = timestep });
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

            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsGIPathtrace();
                });

            ExecuteCommandList(nri::eCommandContextType_Graphics, backbufferIndex,
                [this]
                {
                    m_deferredRenderer.SubmitCommandsSVGFDenoising();
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
        }
        m_deferredRenderer.EndFrame();

        m_swapchain.Present(FALSE);
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
        //// Move to the next frame;
        //UINT backbufferIndex = NextFrame();

        //nri::NRIDevice& device = nri::NRIDevice::Get();
        //nri::CommandAllocatorPool& commandAllocatorPool = device.GetCommandAllocatorPool(nri::eCommandContextType_Graphics);
        //nri::Rc<ID3D12CommandAllocator> commandAllocator = commandAllocatorPool.QueryAllocator();

        //ID3D12GraphicsCommandList4* commandList = m_commandList.Get();

        //nri::ThrowIfFailed(commandList->Reset(commandAllocator.Get(), nullptr));
        //{
        //    nri::UiContext::Get()->BeginFrame();
        //    m_raytracer.PopulateCommandLists(commandList, backbufferIndex, timestep);
        //    nri::UiContext::Get()->EndFrame();
        //    nri::UiContext::Get()->SubmitCommands(backbufferIndex, commandList, &m_swapchain);
        //}
        //nri::ThrowIfFailed(commandList->Close());

        //SubmitCommandList(nri::eCommandContextType_Graphics, commandList, m_fence.Get(), m_fenceValues[backbufferIndex]);
        //commandAllocatorPool.DiscardAllocator(commandAllocator, m_fence.Get(), m_fenceValues[backbufferIndex]);

        //m_swapchain.Present(FALSE);
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
            //m_raytracer.Resize(width, height); // Finally resize the ray tracing scene
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