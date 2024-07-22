#include "ConstantBuffer.h"

#include "../common/Defines.h"
#include "Device.h"

namespace Neb::nri
{

    BOOL ConstantBuffer::Init(const ConstantBufferDesc& desc) noexcept
    {
        m_desc = desc;

        NRIDevice& device = NRIDevice::Get();

        D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(D3D12_RESOURCE_ALLOCATION_INFO{
            .SizeInBytes = desc.NumBuffers * desc.NumBytesPerBuffer,
            .Alignment = 0 });

        // TODO: In future we might move D3D12MA::ALLOCATION_DESC to ConstantBufferDesc?
        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocationDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_UPLOAD, // It alright to use upload heap for constant buffers (i guess?)
        };

        ThrowIfFailed(device.GetResourceAllocator()->CreateResource(
            &allocationDesc,
            &resourceDesc,
            desc.InitialStates,
            nullptr,
            m_bufferAllocation.ReleaseAndGetAddressOf(),
            __uuidof(nullptr), nullptr));

        m_mappings.clear();
        m_mappings.resize(desc.NumBuffers);
        m_descriptorAllocation =
            device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(desc.NumBuffers);

        LPVOID bufferBaseMapping = nullptr;
        ThrowIfFailed(m_bufferAllocation->GetResource()->Map(0, nullptr, &bufferBaseMapping));

        const D3D12_GPU_VIRTUAL_ADDRESS bufferBaseGpuAddress = m_bufferAllocation->GetResource()->GetGPUVirtualAddress();
        for (size_t i = 0; i < desc.NumBuffers; ++i)
        {
            // Init mapping address for every buffer
            m_mappings[i] = reinterpret_cast<std::byte*>(bufferBaseMapping) + (desc.NumBytesPerBuffer * i);

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
                .BufferLocation = bufferBaseGpuAddress + (desc.NumBytesPerBuffer * i),
                .SizeInBytes = static_cast<UINT>(desc.NumBytesPerBuffer)
            };
            device.GetDevice()->CreateConstantBufferView(&cbvDesc, m_descriptorAllocation.CpuAt(i));
        }

        return TRUE;
    }

    D3D12_GPU_VIRTUAL_ADDRESS ConstantBuffer::GetGpuVirtualAddress(SIZE_T bufferIndex) const
    {
        return m_bufferAllocation->GetResource()->GetGPUVirtualAddress() + (m_desc.NumBytesPerBuffer * bufferIndex);
    }

} // Neb::nri namespace