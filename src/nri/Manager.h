#pragma once

#include <D3D12MA/D3D12MemAlloc.h>
#include "stdafx.h"
#include "DescriptorHeap.h"
#include "Swapchain.h"

namespace Neb::nri
{

    struct ManagerDesc
    {
        HWND Handle = NULL;
    };

    class Manager
    {
    public:
        Manager(const ManagerDesc& desc);

        // General D3D12-related calls
        ID3D12Device* GetDevice() { return m_device.Get(); }

        ID3D12CommandQueue* GetGraphicsQueue() { return m_graphicsQueue.Get(); }
        ID3D12CommandQueue* GetCopyQueue() { return m_copyQueue.Get(); }
        ID3D12CommandQueue* GetComputeQueue() { return m_computeQueue.Get(); }

        ID3D12CommandAllocator* GetGraphicsCommandAllocator() { return m_graphicsCommandAllocator.Get(); }
        ID3D12CommandAllocator* GetCopyCommandAllocator() { return m_copyCommandAllocator.Get(); }
        ID3D12CommandAllocator* GetComputeCommandAllocator() { return m_graphicsCommandAllocator.Get(); }

        DescriptorHeap& GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) { return m_descriptorHeaps[type]; }
        const DescriptorHeap& GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_descriptorHeaps[type]; }

        // Swapchain-related calls
        Swapchain& GetSwapchain() { return m_swapchain; }
        const Swapchain& GetSwapchain() const { return m_swapchain; }

        // Resource-management calls
        D3D12MA::Allocator* GetResourceAllocator() { return m_D3D12Allocator.Get(); }

    private:
        // All-in-One initialization of D3D12 stuff
        // Below these init functions goes raw D3D12 functions
        void Init(const ManagerDesc& desc);
        void InitPipeline(const ManagerDesc& desc);

        D3D12Rc<ID3D12Debug1>  m_debugInterface;
        D3D12Rc<IDXGIFactory6> m_dxgiFactory;

        // Most suitable adapter for device creation
        BOOL IsDxgiAdapterMeshShaderSupported(D3D12Rc<ID3D12Device> device) const;
        BOOL IsDxgiAdapterRaytracingSupported(D3D12Rc<ID3D12Device> device) const;
        BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* dxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
        BOOL QueryMostSuitableDeviceAdapter();
        D3D12Rc<IDXGIAdapter3> m_dxgiAdapter;
        D3D12Rc<ID3D12Device>  m_device;

        void InitCommandQueuesAndAllocators();
        D3D12Rc<ID3D12CommandQueue> m_graphicsQueue;
        D3D12Rc<ID3D12CommandQueue> m_copyQueue;
        D3D12Rc<ID3D12CommandQueue> m_computeQueue;
        D3D12Rc<ID3D12CommandAllocator> m_graphicsCommandAllocator;
        D3D12Rc<ID3D12CommandAllocator> m_copyCommandAllocator;
        D3D12Rc<ID3D12CommandAllocator> m_computeCommandAllocator;

        void InitDescriptorHeaps();
        DescriptorHeap m_descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

        void InitSwapchain(HWND hwnd);
        Swapchain m_swapchain;

        void InitResourceAllocator();
        D3D12Rc<D3D12MA::Allocator> m_D3D12Allocator;
    };

}; // Neb::nri namespace