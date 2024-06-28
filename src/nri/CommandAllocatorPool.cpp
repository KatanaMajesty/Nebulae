#include "CommandAllocatorPool.h"

#include "../common/Defines.h"

namespace Neb::nri
{

    D3D12Rc<ID3D12CommandAllocator> CommandAllocatorPool::QueryAllocator()
    {
        NEB_ASSERT(m_device != NULL);

        D3D12Rc<ID3D12CommandAllocator> allocator;
        if (!m_discardedAllocators.empty())
        {
            DiscardedAllocatorEntry& entry = m_discardedAllocators.front();
            if (entry.IsCompleted())
            {
                allocator = entry.Allocator;
                ThrowIfFailed(allocator->Reset());
                m_discardedAllocators.pop();
                return allocator;
            }
        }

        // Create new allocator
        ThrowIfFailed(m_device->CreateCommandAllocator(m_type, IID_PPV_ARGS(allocator.GetAddressOf())));
        return allocator;
    }

    void CommandAllocatorPool::DiscardAllocator(D3D12Rc<ID3D12CommandAllocator> allocator, ID3D12Fence* fence, UINT64 fenceValue)
    {
        m_discardedAllocators.push(DiscardedAllocatorEntry{
                .Fence = fence,
                .FenceValue = fenceValue,
                .Allocator = allocator,
            });
        NEB_ASSERT(m_discardedAllocators.back().IsValid()); // should be valid!
    }

    BOOL CommandAllocatorPool::DiscardedAllocatorEntry::IsCompleted() const
    {
        NEB_ASSERT(IsValid()); // cannot be invalid!
        return Fence->GetCompletedValue() >= FenceValue;
    }

} // Neb::nri namespace