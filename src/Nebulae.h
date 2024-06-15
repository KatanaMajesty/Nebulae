#pragma once

#include "common/TimeWatch.h"

#include "core/GLTFScene.h"
#include "core/GLTFSceneImporter.h"

#include "nri/DepthStencilBuffer.h"
#include "nri/Manager.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

namespace Neb
{

    struct AppSpec
    {
        HWND Handle = NULL;
        std::filesystem::path AssetsDirectory;
    };

    // All CBs require 256 alignment
    struct alignas(256) CbInstanceInfo
    {
        Neb::Mat4 InstanceToWorld;
        Neb::Mat4 Projection;
    };

    class Nebulae
    {
    private:
        Nebulae() = default;

    public:
        Nebulae(const Nebulae&) = delete;
        Nebulae& operator=(const Nebulae&) = delete;

        static Nebulae& Get();
    
        inline BOOL IsInitialized() const { return m_isInitialized; }

        BOOL Init(const AppSpec& appSpec);
        void Resize(UINT width, UINT height);
        void Render(GLTFScene* scene);

        // TODO: Temporarily here, will be removed asap
        GLTFSceneImporter& GetSceneImporter() { return m_sceneImporter; }

    private:
        BOOL m_isInitialized = FALSE;

        TimeWatch<dur::Milliseconds> m_timeWatch;
        int64_t m_lastFrameSeconds = {};

        nri::ShaderCompiler m_shaderCompiler;

        void WaitForFrameToFinish();
        nri::Swapchain m_swapchain;
        nri::DepthStencilBuffer m_depthStencilBuffer;

        GLTFSceneImporter m_sceneImporter;

        nri::D3D12Rc<ID3D12GraphicsCommandList> m_commandList;
        nri::D3D12Rc<ID3D12RootSignature> m_rootSignature;
        nri::D3D12Rc<ID3D12PipelineState> m_pipelineState;

        void InitInstanceInfoCb();
        CbInstanceInfo* m_cbInstanceInfoBufferMapping = nullptr;
        nri::D3D12Rc<ID3D12Resource> m_cbInstanceInfoBuffer;
        nri::DescriptorAllocation m_cbInstanceInfoDescriptor;
    };

}