#include "UiContext.h"

#include "core/Math.h"
#include "common/Assert.h"
#include "common/Log.h"

#include "nri/DescriptorHeap.h"
#include "nri/DescriptorHeapAllocation.h"

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx12.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Neb::nri
{

    void UiContext::Init(const UiSpecification& spec)
    {
        NEB_ASSERT(spec.handle && spec.device, "Cannot be null!");
        m_spec = spec;

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();

        NEB_LOG_WARN_IF(ImGui::GetCurrentContext() != nullptr, "ImGui was already initialized! Re-initializing again, was it intended?");
        m_internalContext = ImGui::CreateContext();

        // Setup styles
        ImGui::StyleColorsDark();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

        this->InitWin32Hwnd();
        this->InitDx12();
    }

    void UiContext::Shutdown()
    {
        // Cleanup
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    void UiContext::BeginFrame()
    {
        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void UiContext::EndFrame()
    {
        ImGui::Render();
    }

    void UiContext::SubmitCommands(UINT frameIndex, ID3D12GraphicsCommandList* commandList, Swapchain* swapchain)
    {
        NEB_ASSERT(commandList && swapchain);
        ID3D12DescriptorHeap* srvHeap = m_spec.device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap();

        static const Neb::Vec4 rtvClearColor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptor = swapchain->GetCurrentBackbufferRtvHandle();
        commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, nullptr);
        commandList->SetDescriptorHeaps(1, &srvHeap);

        ID3D12Resource* backbuffer = swapchain->GetBackbuffer(frameIndex);

        // Render ImGui graphics
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commandList->ResourceBarrier(1, &barrier);
        {
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
        }
        barrier = CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        commandList->ResourceBarrier(1, &barrier);
    }

    void UiContext::HandleWin32Proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (this->IsInitialized())
        {
            NEB_ASSERT(hWnd == m_spec.handle);
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }
    }

    void UiContext::InitWin32Hwnd()
    {
        // Setup Platform backend
        if (!ImGui_ImplWin32_Init(m_spec.handle))
        {
            NEB_LOG_ERROR("failed to init ImGui impl for win32");
            NEB_ASSERT(false);
        }
    }

    void UiContext::InitDx12()
    {
        NEB_ASSERT(m_spec.device, "Device should be valid to initialize UI context");
        NRIDevice* device = m_spec.device;

        // Setup Renderer backend
        ImGui_ImplDX12_InitInfo initInfo = {};
        initInfo.UserData = static_cast<void*>(device);
        initInfo.Device = device->GetDevice();
        initInfo.CommandQueue = device->GetCommandQueue(eCommandContextType_Graphics);
        initInfo.NumFramesInFlight = m_spec.numInflightFrames;
        initInfo.RTVFormat = m_spec.renderTargetFormat;
        initInfo.DSVFormat = m_spec.depthStencilFormat;

        // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
        // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
        initInfo.SrvDescriptorHeap = device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap();
        initInfo.SrvDescriptorAllocFn = 
        [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
        { 
            NRIDevice* device = reinterpret_cast<NRIDevice*>(info->UserData);
            DescriptorHeapAllocation alloc = device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(1);
            *outCpuHandle = alloc.CpuAddress;
            *outGpuHandle = alloc.GpuAddress;
        };
        initInfo.SrvDescriptorFreeFn = 
        [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
        { 
            NRIDevice* device = reinterpret_cast<NRIDevice*>(info->UserData);

            DescriptorHeap& heap = device->GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            NEB_ASSERT(heap.IsValidCPUDescriptorHandle(cpuHandle), "Cpu handle should be valid!");
            NEB_ASSERT(heap.IsValidGPUDescriptorHandle(gpuHandle), "Gpu handle should be valid!");

            // TODO: Add release/free descriptor allocation
        };

        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            NEB_LOG_ERROR("failed to init ImGui impl for dx12");
            NEB_ASSERT(false);
        }
    }

}; // Neb::nri namespace