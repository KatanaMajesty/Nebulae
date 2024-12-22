#pragma once

#include "nri/stdafx.h"
#include "nri/Device.h"
#include "nri/Swapchain.h"

#include <imgui/imgui.h>

namespace Neb::nri
{

    struct UiSpecification
    {
        HWND handle = NULL;
        NRIDevice* device = nullptr;

        UINT numInflightFrames = 1;
        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
    };

    class UiContext
    {
    private:
        UiContext() = default;

    public:
        UiContext(const UiContext&) = delete;
        UiContext& operator=(const UiContext&) = delete;

        static UiContext* Get() noexcept
        {
            static UiContext instance;
            return &instance;
        }

        // Initializes ImGui context, throwing if failed
        void Init(const UiSpecification& spec);
        void Shutdown();

        bool IsInitialized() const { return m_internalContext != nullptr; }

        void BeginFrame();
        void EndFrame();
        void SubmitCommands(UINT frameIndex, ID3D12GraphicsCommandList* commandList, Swapchain* swapchain);

        void HandleWin32Proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        // returns  true - if mouse cursor is currently hovering over/being used by ImGui UI,
        //          false otherwise
        bool IsMouseBusy();

    private:
        void InitWin32Hwnd();
        void InitDx12();

        UiSpecification m_spec = UiSpecification();
        ImGuiContext* m_internalContext = nullptr;
    };

}; // Neb::nri namespace