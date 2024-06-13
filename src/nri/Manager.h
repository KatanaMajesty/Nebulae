#pragma once

#include "stdafx.h" // Include before memory allocator
#include <D3D12MA/D3D12MemAlloc.h>
#include <memory>

#include "DescriptorHeap.h"

namespace Neb::nri
{

    enum ECommandContextType
    {
        eCommandContextType_Graphics = 0,
        eCommandContextType_Copy,
        eCommandContextType_Compute,
        eCommandContextType_NumTypes,
    };

    class Manager
    {
    private:
        Manager() = default;

    public:
        Manager(const Manager&) = delete;
        Manager& operator=(const Manager&) = delete;

        static Manager& Get();
    
        // Main member-function. Initializes the entire manager (device)
        void Init();

        // Helper calls
        IDXGIFactory6* GetDxgiFactory() { return m_dxgiFactory.Get(); }

        // General D3D12-related calls
        ID3D12Device4* GetDevice() { return m_device.Get(); }

        // Command context related calls
        ID3D12CommandQueue* GetCommandQueue(ECommandContextType contextType) { return m_commandQueues[contextType].Get(); }
        ID3D12CommandAllocator* GetCommandAllocator(ECommandContextType contextType) { return m_commandAllocators[contextType].Get(); }
        ID3D12Fence* GetFence(ECommandContextType contextType) { return m_fences[contextType].Get(); }
        UINT64& GetFenceValue(ECommandContextType contextType) { return m_fenceValues[contextType]; }
        const UINT64& GetFenceValue(ECommandContextType contextType) const { return m_fenceValues[contextType]; }

        DescriptorHeap& GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) { return m_descriptorHeaps[type]; }
        const DescriptorHeap& GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const { return m_descriptorHeaps[type]; }

        // Resource-management calls
        D3D12MA::Allocator* GetResourceAllocator() { return m_D3D12Allocator.Get(); }

    private:
        // All-in-One initialization of D3D12 stuff
        // Below these init functions goes raw D3D12 functions
        void InitPipeline();

        D3D12Rc<ID3D12Debug1> m_debugInterface;
        D3D12Rc<IDXGIFactory6> m_dxgiFactory;

        // Most suitable adapter for device creation
        BOOL IsDxgiAdapterMeshShaderSupported(D3D12Rc<ID3D12Device> device) const;
        BOOL IsDxgiAdapterRaytracingSupported(D3D12Rc<ID3D12Device> device) const;
        BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* dxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
        BOOL QueryMostSuitableDeviceAdapter();
        D3D12Rc<IDXGIAdapter3> m_dxgiAdapter;
        D3D12Rc<ID3D12Device4> m_device;

        void InitCommandContexts();
        D3D12Rc<ID3D12CommandQueue> m_commandQueues[eCommandContextType_NumTypes];
        D3D12Rc<ID3D12CommandAllocator> m_commandAllocators[eCommandContextType_NumTypes];
        D3D12Rc<ID3D12Fence> m_fences[eCommandContextType_NumTypes];
        UINT64 m_fenceValues[eCommandContextType_NumTypes];

        void InitDescriptorHeaps();
        DescriptorHeap m_descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

        void InitResourceAllocator();
        D3D12Rc<D3D12MA::Allocator> m_D3D12Allocator;
    };

}; // Neb::nri namespace
