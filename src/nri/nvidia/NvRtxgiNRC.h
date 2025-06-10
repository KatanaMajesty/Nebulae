#pragma once

#include "nri/stdafx.h"
#include "util/Types.h"

#include "NrcCommon.h"
#include "NrcD3d12.h"
#include "NrcStructures.h"

#include <array>

namespace Neb::nri
{
    class NvRtxgiNRCIntegration
    {
    private:
        NvRtxgiNRCIntegration() = default;
    
    public:
        NvRtxgiNRCIntegration(const NvRtxgiNRCIntegration&) = delete;
        NvRtxgiNRCIntegration& operator=(const NvRtxgiNRCIntegration&) = delete;

        static NvRtxgiNRCIntegration* Get()
        {
            static NvRtxgiNRCIntegration instance;
            return &instance;
        }

        void Init();
        bool IsInitialised() const { return m_nrcContext != nullptr; }
        void Destroy();

        void Configure(const nrc::ContextSettings& settings);
        void BeginFrame(Rc<ID3D12GraphicsCommandList4> commandList, const nrc::FrameSettings& frameSettings);
        void EndFrame(ID3D12CommandQueue* commandQueue);

        // optionally returns a training loss that is a normalized float and is specified as pTrainingLoss
        void QueryAndTrain(Rc<ID3D12GraphicsCommandList4> commandList, float* pTrainingLoss);
        // modulates radiance results queried from training and adds it to the outputResource, which should represent a 'final image'
        void Resolve(Rc<ID3D12GraphicsCommandList4> commandList, ID3D12Resource* outputResource);

        // Populate NrcConstants(shader constants structure).
        // NrcConstants should be put into a constant buffer and passed to NRC_InitializeNRCParameters* in your shaders.
        void PopulateShaderConstants(NrcConstants* pOutConstants) const;
        
        // returns the amount of memory currently occupied by NRC in bytes
        size_t GetMemoryFootprint() const;

        const nrc::ContextSettings& GetContextSettings() const { return m_contextSettings; }
        
        struct NRCBuffer
        {
            Rc<ID3D12Resource> resource;
            nrc::AllocationInfo info = {};
            size_t allocationSizeInBytes = 0;
        };
        const NRCBuffer& GetNRCBuffer(nrc::BufferIdx index) const { return m_nrcBuffers.at(EnumValue(index)); }

    private:
        nrc::d3d12::Context* m_nrcContext = nullptr;
        nrc::ContextSettings m_contextSettings;

        std::array<NRCBuffer, EnumValue(nrc::BufferIdx::Count)> m_nrcBuffers;
    };
} // Neb::nri namespace