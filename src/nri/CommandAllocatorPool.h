#pragma once

#include <queue>
#include "stdafx.h"

namespace Neb::nri
{

    // The idea of command allocator pool is to simplify and allow for simple
    // command allocator reusability abstraction
    //
    // The implementation approach is as follows:
    // -    The main method, across which the pool is designed is QueryAllocator(), this method will handle
    //      all the behavior related to command allocator retrieval
    //
    class CommandAllocatorPool
    {
    public:
        CommandAllocatorPool() = default;
        CommandAllocatorPool(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
            : m_device(device)
            , m_type(type)
        {
        }

        // Querying an allocator will first check for any available allocator in the pool
        // if such allocator exists - it will be returned. Otherwise, new allocator is created
        //
        // REMARK: The command allocator returned will already be reset
        D3D12Rc<ID3D12CommandAllocator> QueryAllocator();

        // Discarding an allocator allows for easier command allocator management.
        // By storing fence and value associated with command execution
        // we can check whether or not command allocator is still needed (in queue).
        // If such allocator is no more used (fenceValue is completed) we can return it in the next QueryAllocator() call
        void DiscardAllocator(D3D12Rc<ID3D12CommandAllocator> allocator, ID3D12Fence* fence, UINT64 fenceValue);

    private:
        struct DiscardedAllocatorEntry
        {
            BOOL IsValid() const { return Fence != NULL && Allocator != NULL; }
            BOOL IsCompleted() const;

            ID3D12Fence* Fence = NULL;
            UINT64 FenceValue = 0;

            // The allocator to be queried upon
            D3D12Rc<ID3D12CommandAllocator> Allocator = NULL;
        };

        ID3D12Device* m_device = NULL;
        D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        std::queue<DiscardedAllocatorEntry> m_discardedAllocators;
    };

} // Neb::nri