#pragma once

#include "stdafx.h"

class RayTracer
{
public:
    static constexpr UINT NumBackbuffers = 3;

    RayTracer() = default;

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    RayTracer(RayTracer&&) = delete;
    RayTracer& operator=(RayTracer&&) = delete;

    void Init(HWND hwnd);

private:
    void InitPipeline(HWND hwnd);

    ComPtr<ID3D12Debug1>  m_DebugInterface;
    ComPtr<IDXGIFactory6> m_DxgiFactory;

    // Most suitable adapter for device creation
    BOOL IsDxgiAdapterRaytracingSupported(ComPtr<ID3D12Device> device) const;
    BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* DxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
    BOOL QueryMostSuitableDeviceAdapter();
    ComPtr<IDXGIAdapter3> m_DxgiAdapter;
    ComPtr<ID3D12Device>  m_Device;
    
    void InitCommandQueues();
    ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
    ComPtr<ID3D12CommandQueue> m_CopyQueue;
    ComPtr<ID3D12CommandQueue> m_ComputeQueue;

    void InitSwapchain(HWND hwnd);
    ComPtr<IDXGISwapChain1> m_DxgiSwapchain;
};