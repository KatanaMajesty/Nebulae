#pragma once

#include "stdafx.h"
#include <type_traits>
#include "common/Assert.h"

#define USE_PIX
#include <WinPixEventRuntime/pix3.h>

namespace Neb::nri
{

    // For more info https://devblogs.microsoft.com/pix/winpixeventruntime/
    // TODO: Implement programmatic captures https://devblogs.microsoft.com/pix/programmatic-capture/

    template<typename... Args>
    void BeginEvent(ID3D12GraphicsCommandList* commandList, std::string_view name, Args&&... args)
    {
        PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void BeginEvent(ID3D12CommandQueue* commandQueue, std::string_view name, Args&&... args)
    {
        PIXBeginEvent(commandQueue, PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void BeginEvent(std::string_view name, Args&&... args)
    {
        PIXBeginEvent(PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    inline void EndEvent(ID3D12GraphicsCommandList* commandList) { PIXEndEvent(commandList); }
    inline void EndEvent(ID3D12CommandQueue* commandQueue) { PIXEndEvent(commandQueue); }
    inline void EndEvent() { PIXEndEvent(); }

    template<typename... Args>
    void SetMarker(ID3D12GraphicsCommandList* commandList, std::string_view name, Args&&... args)
    {
        PIXSetMarker(commandList, PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void SetMarker(ID3D12CommandQueue* commandQueue, std::string_view name, Args&&... args)
    {
        PIXSetMarker(commandQueue, PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void SetMarker(std::string_view name, Args&&... args)
    {
        PIXSetMarker(PIX_COLOR_DEFAULT, name.data(), std::forward<Args>(args)...);
    }

    inline void NotifyFenceWakeup(HANDLE event) { PIXNotifyWakeFromFenceSignal(event); }

    template<class T>
    struct InferEventType
    {
        using Type = void;
    };

    template<>
    struct InferEventType<ID3D12CommandQueue*>
    {
        using Type = ID3D12CommandQueue*;
    };

    template<>
    struct InferEventType<ID3D12GraphicsCommandList*>
    {
        using Type = ID3D12GraphicsCommandList*;
    };

    template<typename T, typename... Args>
    concept HasBeginEvent = requires(T t, Args&&... args) {
        BeginEvent(t, std::forward<Args>(args)...);
    };

    template<typename T>
    concept HasEndEvent = requires(T t) {
        EndEvent(t);
    };

    template<typename ContextType>
    struct ScopedEvent
    {
        template<typename... Args>
            requires HasBeginEvent<ContextType, Args...>
        ScopedEvent(ContextType context, Args&&... args)
            : Context(context)
        {
            NEB_ASSERT(context != nullptr);
            BeginEvent(context, std::forward<Args>(args)...);
        }

        ~ScopedEvent()
        {
            if constexpr (HasEndEvent<ContextType>)
            {
                EndEvent(this->Context);
            }
            else
            {
                EndEvent();
            }
        }

        ContextType Context;
    };
}

#define NEB_PIX_GET_SCOPED_EVENT_VARNAME(a, b) a##b
#define NEB_PIX_SCOPED_EVENT(context, ...) \
    ::Neb::nri::ScopedEvent                \
    NEB_PIX_GET_SCOPED_EVENT_VARNAME(NEBULAE_EVENT, __LINE__)(context, ##__VA_ARGS__)