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
        void BeginFrame();
        void EndFrame();
        
        void SubmitTemporalAccumulation(ID3D12GraphicsCommandList4* commandList);
        void SubmitATrousComputeWavelet();

    private:
        // Spatio-temporal variance filtering for pathtracer
        void InitSVGFShadersAndPSO();
        void InitSVGFResources();
        void InitSVGFDescriptors();

        bool m_initialized = false;
        UINT m_width = 0;
        UINT m_height = 0;

        enum ESVGFTemporalRoots
        {
            SVGF_TEMPORAL_ROOT_CONSTANTS = 0,
            SVGF_TEMPORAL_ROOT_SRV,
            SVGF_TEMPORAL_ROOT_UAV,
            SVGF_TEMPORAL_ROOT_NUM_ROOTS,
        };
        nri::Shader m_csTemporalSVGF;
        nri::RootSignature m_svgfTemporalRS;
        nri::Rc<ID3D12PipelineState> m_svgfTemporalPSO;

        // t_RadianceHistory : register(t0, space0);
        // t_Depth           : register(t1, space0);
        // t_DepthHistory    : register(t2, space0);
        // t_Normal          : register(t3, space0);
        // t_NormalHistory   : register(t4, space0);
        // t_MomentHistory   : register(t5, space0);
        nri::DescriptorHeapAllocation m_svgfTemporalSrvHeap;
        // t_Radiance : register(u0, space0);
        // t_Moment   : register(u1, space0);
        // t_Variance : register(u2, space0);
        nri::DescriptorHeapAllocation m_svgfTemporalUavHeap;
        nri::Rc<ID3D12Resource> m_radianceHistory;
        nri::Rc<ID3D12Resource> m_depthHistory;
        nri::Rc<ID3D12Resource> m_normalHistory;
        nri::Rc<ID3D12Resource> m_momentHistory;
        nri::Rc<ID3D12Resource> m_varianceTexture;

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
    };

} // Neb namespace