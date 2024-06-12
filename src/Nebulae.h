#pragma once

#include "nri/Manager.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"
#include "nri/Swapchain.h"

#include "core/GLTFScene.h"
#include "core/GLTFSceneImporter.h"

namespace Neb
{

    struct HelloTriangle
    {
        CD3DX12_VIEWPORT m_viewport;
        CD3DX12_RECT m_scissorRect;
    };

    class Nebulae
    {
    public:
        Nebulae() = default;

        Nebulae(const Nebulae&) = delete;
        Nebulae& operator=(const Nebulae&) = delete;

        BOOL Init(HWND hwnd);
        void Resize(UINT width, UINT height);
        void Render();

        // TODO: Temporarily here, will be removed asap
        GLTFSceneImporter& GetSceneImporter() { return m_sceneImporter; }

    private:
        nri::ShaderCompiler m_shaderCompiler;

        nri::Manager m_nriManager;
        nri::Swapchain m_swapchain;

        GLTFSceneImporter m_sceneImporter;
    };

}