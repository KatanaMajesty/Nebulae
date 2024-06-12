#pragma once

#include "nri/Manager.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

#include "core/GLTFScene.h"
#include "core/GLTFSceneImporter.h"

namespace Neb
{

    struct AppSpec
    {
        HWND Handle = NULL;
        std::filesystem::path AssetsDirectory;
    };

    class Nebulae
    {
    public:
        Nebulae() = default;

        Nebulae(const Nebulae&) = delete;
        Nebulae& operator=(const Nebulae&) = delete;

        BOOL Init(const AppSpec& appSpec);
        void Resize(UINT width, UINT height);
        void Render(GLTFScene* scene);

        // TODO: Temporarily here, will be removed asap
        GLTFSceneImporter& GetSceneImporter() { return m_sceneImporter; }

    private:
        nri::ShaderCompiler m_shaderCompiler;

        nri::Manager m_nriManager;
        nri::Swapchain m_swapchain;

        GLTFSceneImporter m_sceneImporter;

        HANDLE m_fenceEvent = NULL;
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_commandList;
        nri::D3D12Rc<ID3D12RootSignature> m_rootSignature;
        nri::D3D12Rc<ID3D12PipelineState> m_pipelineState;
    };

}