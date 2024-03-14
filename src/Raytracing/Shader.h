#pragma once

#include "stdafx.h"
#include <dxcapi.h>

// https://developer.nvidia.com/dx12-dos-and-donts

enum class EShaderModel
{
    sm_6_5, // nothing else is needed
};

// We do not use Hull, Domain, Geometry shaders here (at least for now)
enum class EShaderType
{
    Vertex,
    Amplification,
    Mesh,
    Pixel,
    Compute,
};

// Shader is just bytecode + bytecode size. we use a tool called ShaderCompiler to get a Shader object
class NebShader
{
public:
    NebShader() = default;
    NebShader(EShaderType type, ComPtr<IDxcBlob> binary, ComPtr<IDxcBlob> pdb, ComPtr<IDxcBlob> reflection)
        : m_Type(type)
        , m_Binary(binary)
        , m_Pdb(pdb)
        , m_Reflection(reflection)
    {
    }

    EShaderType GetType()   const { return m_Type; }
    BOOL        HasBinary() const { return m_Binary != nullptr; }
    
    D3D12_SHADER_BYTECODE GetBinaryBytecode() { return CD3DX12_SHADER_BYTECODE(GetBinaryPointer(), GetBinarySize()); }
    LPVOID GetBinaryPointer() const;
    SIZE_T GetBinarySize()    const;

private:
    EShaderType m_Type;
    ComPtr<IDxcBlob> m_Binary;
    ComPtr<IDxcBlob> m_Pdb;
    ComPtr<IDxcBlob> m_Reflection;
};