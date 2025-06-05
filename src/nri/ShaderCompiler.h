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

        // scalars were introduced in HLSL Shader Model 6.2
        // adds such types as float16_t, uint16_t, int16_t and respective tensors
        // see: https://github.com/microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types
        //
        // adds '-enable-16bit-types' during compilation
        eShaderCompilationFlag_Enable16BitTypes = 4,
    };

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
        std::string_view EntryPoint;
        EShaderModel ShaderModel = EShaderModel::sm_6_5;
        EShaderType ShaderType = EShaderType::Unknown;
    };

    // DXIL library compilation description
    //
    struct LibraryCompilationDesc
    {
        LibraryCompilationDesc() = default;
        LibraryCompilationDesc(EShaderModel shaderModel)
            : ShaderModel(shaderModel)
        {
        }

        EShaderModel ShaderModel = EShaderModel::sm_6_5;
    };

    class ShaderCompiler
    {
    private:
        ShaderCompiler();

    public:
        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

        ~ShaderCompiler() = default;

        static ShaderCompiler* Get()
        {
            static ShaderCompiler compiler;
            return &compiler;
        }

        Shader CompileShader(std::string_view filepath,
            const ShaderCompilationDesc& desc,
            EShaderCompilationFlags flags = eShaderCompilationFlag_None);

        Shader CompileLibrary(std::string_view filepath,
            const LibraryCompilationDesc& desc = LibraryCompilationDesc(),
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
