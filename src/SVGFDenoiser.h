#pragma once

#include "nri/stdafx.h"
#include "nri/Shader.h"
#include "nri/RootSignature.h"
#include "nri/DescriptorHeapAllocation.h"

namespace Neb
{

    class SVGFDenoiser
    {
    public:
        bool IsInitialized() const { return m_initialized; }

        bool Init(UINT width, UINT height);
        bool Resize(UINT width, UINT height);

        // This should be called to properly query gbuffer resources
        void BeginFrame(UINT frameIndex);
        void EndFrame();
        
        // These should be called in the scope of BeginFrame/EndFrame
        uint32_t GetCurrentResourceIndex() const { return m_currentIndex; }
        uint32_t GetHistoryResourceIndex() const { return m_historyIndex; }

        ID3D12Resource* GetRadianceTexture(uint32_t index) const { return m_radiance[index].Get(); }
        ID3D12Resource* GetCurrentRadianceTexture() const { return GetRadianceTexture(GetCurrentResourceIndex()); }
        ID3D12Resource* GetHistoryRadianceTexture() const { return GetRadianceTexture(GetHistoryResourceIndex()); }

        ID3D12Resource* GetDepthArray() const { return m_depths.Get(); }
        ID3D12Resource* GetNormalArray() const { return m_normals.Get(); }

        ID3D12Resource* GetMomentsTexture(uint32_t index) const { return m_moments[index].Get(); }
        ID3D12Resource* GetCurrentMomentsTexture() const { return GetMomentsTexture(GetCurrentResourceIndex()); }
        ID3D12Resource* GetHistoryMomentsTexture() const { return GetMomentsTexture(GetHistoryResourceIndex()); }
        ID3D12Resource* GetVarianceTexture() const { return m_variance.Get(); }

        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentRadianceUav() const { return m_radianceSrvUavHeap.GpuAt(FirstRadianceUavIndex + GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentRadianceSrv() const { return m_radianceSrvUavHeap.GpuAt(FirstRadianceSrvIndex + GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistoryRadianceSrv() const { return m_radianceSrvUavHeap.GpuAt(FirstRadianceSrvIndex + GetHistoryResourceIndex()); }

        D3D12_CPU_DESCRIPTOR_HANDLE GetDepthDsv(uint32_t index) const { return m_depthArrayDsvHeap.CpuAt(index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSrv(uint32_t index) const { return m_depthArraySrvHeap.GpuAt(FirstDepthSrvIndex + index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetStencilSrv(uint32_t index) const { return m_depthArraySrvHeap.GpuAt(FirstStencilSrvIndex + index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentDepthSrv() const { return GetDepthSrv(GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistoryDepthSrv() const { return GetDepthSrv(GetHistoryResourceIndex()); }

        D3D12_CPU_DESCRIPTOR_HANDLE GetNormalRtv(uint32_t index) const { return m_normalArrayRtvHeap.CpuAt(index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSrv(uint32_t index) const { return m_normalArraySrvHeap.GpuAt(index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentNormalSrv() const { return GetNormalSrv(GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistoryNormalSrv() const { return GetNormalSrv(GetHistoryResourceIndex()); }
        
        D3D12_GPU_DESCRIPTOR_HANDLE GetMomentSrv(uint32_t index) const { return m_momentArraySrvUavHeap.GpuAt(FirstMomentSrvIndex + index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetMomentUav(uint32_t index) const { return m_momentArraySrvUavHeap.GpuAt(FirstMomentUavIndex + index); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentMomentUav() const { return GetMomentUav(GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetCurrentMomentSrv() const { return GetMomentSrv(GetCurrentResourceIndex()); }
        D3D12_GPU_DESCRIPTOR_HANDLE GetHistoryMomentSrv() const { return GetMomentSrv(GetHistoryResourceIndex()); }

        D3D12_GPU_DESCRIPTOR_HANDLE GetVarianceUav() const { return m_varianceSrvUavHeap.GpuAt(1); } // 0 srv, 1 uav (only 1 resource, no ping-pong)
        D3D12_GPU_DESCRIPTOR_HANDLE GetVarianceSrv() const { return m_varianceSrvUavHeap.GpuAt(0); } // 0 srv, 1 uav (only 1 resource, no ping-pong)

        DXGI_FORMAT GetNormalFormat() const { return m_normalFormat; }
        DXGI_FORMAT GetRadianceFormat() const { return m_radianceFormat; }

        void ResetHistory(ID3D12GraphicsCommandList4* commandList);
        void SubmitTemporalAccumulation(ID3D12GraphicsCommandList4* commandList);
        void SubmitATrousComputeWavelet();

    private:
        // Spatio-temporal variance filtering for pathtracer
        bool m_initialized = false;
        UINT m_width = 0;
        UINT m_height = 0;

        void InitSVGFShadersAndPSO();

        enum ESVGFATrousRoots
        {
            SVGF_ATROUS_ROOT_CONSTANTS = 0,
            SVGF_ATROUS_ROOT_SRV,
            SVGF_ATROUS_ROOT_OUTPUT,
            SVGF_ATROUS_ROOT_NUM_ROOTS,
        };
        nri::Shader m_csATrousSVGF;
        nri::RootSignature m_svgfATrousRS;
        nri::Rc<ID3D12PipelineState> m_svgfATrousPSO;

        enum ESVGFTemporalRoots
        {
            SVGF_TEMPORAL_ROOT_CONSTANTS = 0,
            SVGF_TEMPORAL_ROOT_RADIANCE_HISTORY_SRV,
            SVGF_TEMPORAL_ROOT_DEPTH_CURRENT_SRV,
            SVGF_TEMPORAL_ROOT_DEPTH_HISTORY_SRV,
            SVGF_TEMPORAL_ROOT_NORMAL_CURRENT_SRV,
            SVGF_TEMPORAL_ROOT_NORMAL_HISTORY_SRV,
            SVGF_TEMPORAL_ROOT_MOMENT_HISTORY_SRV,
            SVGF_TEMPORAL_ROOT_RADIANCE_CURRENT_UAV,
            SVGF_TEMPORAL_ROOT_MOMENT_CURRENT_UAV,
            SVGF_TEMPORAL_ROOT_VARIANCE_UAV,
            SVGF_TEMPORAL_ROOT_NUM_ROOTS,
        };
        nri::Shader m_csTemporalSVGF;
        nri::RootSignature m_svgfTemporalRS;
        nri::Rc<ID3D12PipelineState> m_svgfTemporalPSO;
        
        void InitSVGFResources();

        // Current index represents current frame's resource index (0 or 1)
        uint32_t m_currentIndex; // frameIndex & 1
        // History index represents current frame's history resources (0 or 1).
        // This index is always the opposite of currentIndex
        uint32_t m_historyIndex; // currentIndex ^ 1

        static constexpr uint32_t NumPingPongResources = 2;
        nri::Rc<ID3D12Resource> m_radiance[NumPingPongResources]; // Radiance should be 2D texture, not 2DArray, as it is consumed by NRC
        nri::Rc<ID3D12Resource> m_depths;           // Resources are created as 2D textures with 2 slices
        nri::Rc<ID3D12Resource> m_normals;          // Resources are created as 2D textures with 2 slices
        nri::Rc<ID3D12Resource> m_moments[NumPingPongResources]; // For some reason SRV/UAV on different slices upsets the compiler
        nri::Rc<ID3D12Resource> m_variance;

        // A-Trous output index shows which image in m_svgfATrousTargets is a denoised output image
        uint32_t m_aTrousOutputIndex;

        static constexpr uint32_t numATrousIndices = 2;
        
        // Global descriptors allocate descriptor heaps and descriptors that will be accessed by other parts
        // of the application, such as DepthArray or NormalArray DSV/SRV/UAV, etc.
        void InitSVGFDescriptors();
        
        // General resource information
        DXGI_FORMAT m_depthResourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
        DXGI_FORMAT m_depthDsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DXGI_FORMAT m_depthDepthSrvUavFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        DXGI_FORMAT m_depthStencilSrvUavFormat = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
        DXGI_FORMAT m_normalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        DXGI_FORMAT m_radianceFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        DXGI_FORMAT m_momentFormat = DXGI_FORMAT_R16G16_FLOAT; // m1, m2
        DXGI_FORMAT m_varianceFormat = DXGI_FORMAT_R16_FLOAT; // m1, m2

        static constexpr uint32_t FirstRadianceSrvIndex = 0;
        static constexpr uint32_t FirstRadianceUavIndex = FirstRadianceSrvIndex + NumPingPongResources;
        nri::DescriptorHeapAllocation m_radianceSrvUavHeap;
        static constexpr uint32_t FirstDepthSrvIndex = 0;
        static constexpr uint32_t FirstStencilSrvIndex = FirstDepthSrvIndex + NumPingPongResources;
        nri::DescriptorHeapAllocation m_depthArrayDsvHeap;
        nri::DescriptorHeapAllocation m_depthArraySrvHeap;
        nri::DescriptorHeapAllocation m_normalArrayRtvHeap; // Nebulae is a g-buffer based renderer
        nri::DescriptorHeapAllocation m_normalArraySrvHeap;
        static constexpr uint32_t FirstMomentSrvIndex = 0;
        static constexpr uint32_t FirstMomentUavIndex = FirstMomentSrvIndex + NumPingPongResources;
        nri::DescriptorHeapAllocation m_momentArraySrvUavHeap;
        nri::DescriptorHeapAllocation m_varianceSrvUavHeap; // 0 srv, 1 uav (only 1 resource, no ping-pong)

        struct SVGFTemporalConstants
        {
            uint32_t resolution[2];
            float depthSigma;
            float alpha;       // tweakable stability var
            float varianceEps; // small value
        };
        SVGFTemporalConstants m_temporalConstants;
        // Texture2D<float3> t_RadianceSample  : register(t0, space0);
        // Texture2D<float3> t_RadianceHistory : register(t1, space0);
        // Texture2D<float>  t_Depth           : register(t2, space0);
        // Texture2D<float>  t_DepthHistory    : register(t3, space0);
        // Texture2D<float3> t_Normal          : register(t4, space0);
        // Texture2D<float3> t_NormalHistory   : register(t5, space0);
        // Texture2D<float2> t_MomentHistory   : register(t6, space0);
        nri::DescriptorHeapAllocation m_svgfTemporalSrvHeap;
        // RWTexture2D<float3> t_Radiance : register(u0, space0); 
        // RWTexture2D<float2> t_Moment   : register(u1, space0);
        // RWTexture2D<float>  t_Variance : register(u2, space0);
        nri::DescriptorHeapAllocation m_svgfTemporalUavHeap;
        
        struct SVGFAtrousConstants
        {
            uint32_t resolution[2];
            float step;     // 1, 2, 4, … (powers of 2)
            float phiColor; // nominal constant (k)
            float phiNormal;
            float phiDepth;
        };
        SVGFAtrousConstants m_aTrousConstants;
        // Texture2D<float3> t_Radiance   : register(t0, space0);
        // Texture2D<float>  t_Variance   : register(t1, space0);
        // Texture2D<float>  t_Depth      : register(t2, space0);
        // Texture2D<float3> t_Normal     : register(t3, space0);
        nri::DescriptorHeapAllocation m_svgfATrousSrvHeap;
        // RWTexture2D<float3> u_Output : register(u0, space0);
        nri::DescriptorHeapAllocation m_svgfATrousUavHeap;
    };

} // Neb namespace