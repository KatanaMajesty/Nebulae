#pragma once

#include "../nri/stdafx.h"
#include <dxcapi.h>

// https://developer.nvidia.com/dx12-dos-and-donts

namespace Neb::nri
{

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
    class Shader
    {
    public:
        Shader() = default;
        Shader(EShaderType type, D3D12Rc<IDxcBlob> binary, D3D12Rc<IDxcBlob> pdb, D3D12Rc<IDxcBlob> reflection)
            : m_type(type)
            , m_binary(binary)
            , m_pdb(pdb)
            , m_reflection(reflection)
        {
        }

        EShaderType GetType() const { return m_type; }
        BOOL HasBinary() const { return m_binary != nullptr; }

        D3D12_SHADER_BYTECODE GetBinaryBytecode() { return CD3DX12_SHADER_BYTECODE(GetBinaryPointer(), GetBinarySize()); }
        LPVOID GetBinaryPointer() const;
        SIZE_T GetBinarySize() const;

    private:
        EShaderType m_type;
        D3D12Rc<IDxcBlob> m_binary;
        D3D12Rc<IDxcBlob> m_pdb;
        D3D12Rc<IDxcBlob> m_reflection;
    };

} // Neb::nri namespace