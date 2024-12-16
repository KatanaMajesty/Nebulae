#pragma once

#include "stdafx.h" // Include before memory allocator
#include <D3D12MA/D3D12MemAlloc.h>
#include <memory>

#include "CommandAllocatorPool.h"
#include "DescriptorHeap.h"

namespace Neb::nri
{

    enum class ESupportTier_MeshShader : uint8_t
    {
        NotSupported = 0,
        SupportTier_1_0 = 1,
    };

    enum class ESupportTier_Raytracing : uint8_t
    {
        NotSupported = 0,
        SupportTier_1_0 = 1,
        SupportTier_1_1 = 2
    };

    struct NRIDeviceCapabilities
    {
        BOOL IsScreenTearingSupported = FALSE;
        ESupportTier_MeshShader MeshShaderSupportTier = ESupportTier_MeshShader::NotSupported;
        ESupportTier_Raytracing RaytracingSupportTier = ESupportTier_Raytracing::NotSupported;
    };

    enum ECommandContextType
    {
        eCommandContextType_Graphics = 0,
        eCommandContextType_Copy,
        eCommandContextType_Compute,
        eCommandContextType_NumTypes,
    };

    class NRIDevice
    {
    private:
        NRIDevice() = default;

    public:
        NRIDevice(const NRIDevice&) = delete;
        NRIDevice& operator=(const NRIDevice&) = delete;

        static NRIDevice& Get();

        // Main member-function. Initializes the entire manager (device)
        void Init();
        void Deinit();

        // Helper calls
        IDXGIFactory6* GetDxgiFactory() { return m_dxgiFactory.Get(); }

        // General D3D12-related calls
        ID3D12Device5* GetDevice() { return m_device.Get(); }
        const NRIDeviceCapabilities& GetCapabilities() const { return m_capabilities; }

        // Command context related calls
        ID3D12CommandQueue* GetCommandQueue(ECommandContextType contextType) { return m_commandQueues[contextType].Get(); }
        CommandAllocatorPool& GetCommandAllocatorPool(ECommandContextType contextType) { return m_commandAllocatorPools[contextType]; }

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
        BOOL IsDxgiAdapterSuitable(IDXGIAdapter3* dxgiAdapter, const DXGI_ADAPTER_DESC1& desc) const;
        BOOL QueryMostSuitableDeviceAdapter();
        D3D12Rc<IDXGIAdapter3> m_dxgiAdapter;
        D3D12Rc<ID3D12Device5> m_device;
        D3D12Rc<ID3D12DebugDevice> m_debugDevice;

        NRIDeviceCapabilities m_capabilities = {};
        BOOL QueryDxgiFactoryTearingSupport() const;
        ESupportTier_MeshShader QueryDeviceMeshShaderSupportTier() const;
        ESupportTier_Raytracing QueryDeviceRaytracingSupportTier() const;

        void InitCommandContexts();
        D3D12Rc<ID3D12CommandQueue> m_commandQueues[eCommandContextType_NumTypes];
        CommandAllocatorPool m_commandAllocatorPools[eCommandContextType_NumTypes];

        void InitDescriptorHeaps();
        DescriptorHeap m_descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

        void InitResourceAllocator();
        D3D12Rc<D3D12MA::Allocator> m_D3D12Allocator;
    };

}; // Neb::nri namespace
