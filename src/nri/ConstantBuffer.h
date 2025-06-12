#pragma once

#include <vector>
#include "stdafx.h"
#include "D3D12MA/D3D12MemAlloc.h"
#include "DescriptorHeapAllocation.h"
#include "util/Memory.h"

namespace Neb::nri
{

    struct ConstantBufferDesc
    {
        D3D12_RESOURCE_STATES InitialStates = D3D12_RESOURCE_STATE_GENERIC_READ;
        SIZE_T NumBuffers = 0;
        SIZE_T NumBytesPerBuffer = 0;
    };

    // This constant buffer is suited for inflight frames, creating multiple copies of the underlying D3D12 resource
    class ConstantBuffer
    {
    public:
        BOOL Init(const ConstantBufferDesc& desc) noexcept;
        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress(SIZE_T bufferIndex) const;
    
        LPVOID GetMapping(SIZE_T bufferIndex) const { return m_mappings.at(bufferIndex); }

        template<typename T>
        T* GetMapping(SIZE_T bufferIndex) const { return reinterpret_cast<T*>(GetMapping(bufferIndex)); }

        const DescriptorHeapAllocation& GetDescriptorAllocation() const { return m_descriptorAllocation; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetCBVHandle(SIZE_T bufferIndex) const { return m_descriptorAllocation.CpuAt(bufferIndex); }

        template<typename T>
        void Upload(SIZE_T bufferIndex, const T& t)
        {
            NEB_ASSERT(AlignUp(sizeof(T), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) == m_desc.NumBytesPerBuffer);
            std::memcpy(GetMapping<T>(bufferIndex), &t, sizeof(T));
        }

        template<typename... Args>
        void SetName(std::format_string<Args...> name, Args&&... args)
        {
            NEB_SET_HANDLE_NAME(m_bufferAllocation->GetResource(), name, args...);
        }

    private:
        ConstantBufferDesc m_desc = {};

        // This allocation stores one buffer resource, which size is = NumBuffersInRange * NumBytesPerBuffer,
        // which would avoid multiple allocations at once
        D3D12Rc<D3D12MA::Allocation> m_bufferAllocation;

        // An array of mapping will contain NumBuffersInRange mappings in total, which can almost always be assumed
        // and will be checked by internal API
        std::vector<LPVOID> m_mappings;

        // m_descriptor range stores all NumBuffersInRange CBV descriptors in a single range allocation
        DescriptorHeapAllocation m_descriptorAllocation;
    };

    template<typename T>
    struct ConstantBufferWrap
    {
        void Init(const ConstantBufferDesc& desc)
        {
            cb.Init(desc);
        }

        void Upload(size_t index)
        {
            std::memcpy(cb.GetMapping<T>(index), &data, sizeof(T));
            cb.Upload(index, data);
        }

        T& GetData() { return data; }
        const T& GetData() const { return data; }

        ConstantBuffer cb;
        T data;
    };

} // Neb::nri namespace