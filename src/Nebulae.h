#pragma once

#include "nri/Manager.h"
#include "nri/Shader.h"
#include "nri/ShaderCompiler.h"

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
        Nebulae(HWND hwnd);

        Nebulae(const Nebulae&) = delete;
        Nebulae& operator=(const Nebulae&) = delete;

        void Render();

    private:
        nri::Manager m_nriManager;
        nri::ShaderCompiler m_shaderCompiler;
    };

}