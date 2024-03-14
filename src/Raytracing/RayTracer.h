#pragma once

#include "NRIManager.h"
#include "Shader.h"
#include "ShaderCompiler.h"

class RayTracer
{
public:
    RayTracer(HWND hwnd);

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    RayTracer(RayTracer&&) = delete;
    RayTracer& operator=(RayTracer&&) = delete;

    void Render();

private:
    NRIManager m_NRIManager;
    
    // Shader's are here
    NebShaderCompiler m_ShaderCompiler;
};