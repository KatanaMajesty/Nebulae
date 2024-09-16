#pragma once

#include "stdafx.h"
#include <dxcapi.h>
#include <string>
#include <string_view>

#include "Shader.h"

// For better understanding of DXC https://simoncoenen.com/blog/programming/graphics/DxcCompiling

namespace Neb::nri
{

    using EShaderCompilationFlags = uint32_t;
    enum EShaderCompilationFlag
    {
        eShaderCompilationFlag_None = 0,

        // Strips PDB from the Object
        eShaderCompilationFlag_StripDebug = 1,

        // Strips RDAT from Object
        eShaderCompilationFlag_StripReflect = 2,
    };

    // Shader define should only live in a scope of shader compilation
    // TODO: Maybe somehow replace convertion from std::string_view to std::wstring?
    struct ShaderDefine
    {
        ShaderDefine() = default;
        ShaderDefine(std::string_view name, std::string_view value)
            : Name(name.begin(), name.end())
            , Value(value.begin(), value.end())
        {
        }

        std::wstring Name;
        std::wstring Value;
    };

    struct ShaderCompilationDesc
    {
        ShaderCompilationDesc() = default;
        ShaderCompilationDesc(std::string_view entryPoint, EShaderModel shaderModel, EShaderType shaderType)
            : EntryPoint(entryPoint)
            , ShaderModel(shaderModel)
            , ShaderType(shaderType)
        {
        }

        inline ShaderCompilationDesc& AddDefine(const ShaderDefine& define)
        {
            Defines.push_back(define);
            return *this;
        }

        std::vector<ShaderDefine> Defines;
        const std::string_view EntryPoint;
        const EShaderModel ShaderModel;
        const EShaderType ShaderType;
    };

    class ShaderCompiler
    {
    public:
        ShaderCompiler();

        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        ~ShaderCompiler() = default;

        Shader CompileShader(std::string_view filepath,
            const ShaderCompilationDesc& desc,
            EShaderCompilationFlags flags = eShaderCompilationFlag_None);

    private:
        std::wstring_view GetTargetProfile(EShaderModel shaderModel, EShaderType shaderType) const;

        struct CompilationResult
        {
            D3D12Rc<IDxcBlob> Binary;
            D3D12Rc<IDxcBlob> Pdb;
            D3D12Rc<IDxcBlob> Reflection;
        };

        CompilationResult CompileInternal(
            std::wstring_view filepath,
            std::wstring_view entryPoint,
            std::wstring_view targetProfile,
            const std::vector<DxcDefine>& defines, EShaderCompilationFlags flags);

        D3D12Rc<IDxcUtils> m_dxcUtils;
        D3D12Rc<IDxcCompiler3> m_dxcCompiler;
        D3D12Rc<IDxcIncludeHandler> m_dxcIncludeHandler;
    };

} // Neb namespace
