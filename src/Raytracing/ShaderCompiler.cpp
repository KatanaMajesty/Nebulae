#include "ShaderCompiler.h"

#include <fstream>
#include <filesystem>
#include <array>

NebShaderCompiler::NebShaderCompiler()
{
    ThrowIfFailed( DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(m_DxcUtils.GetAddressOf())) );
    ThrowIfFailed( DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(m_DxcCompiler.GetAddressOf())) );
    ThrowIfFailed( m_DxcUtils->CreateDefaultIncludeHandler(m_DxcIncludeHandler.GetAddressOf()) );
}

NebShader NebShaderCompiler::CompileShader(const std::string& filepath, const ShaderCompilationDesc& desc, EShaderCompilationFlags flags)
{
    std::wstring wFilepath = std::wstring(filepath.begin(), filepath.end());
    std::wstring wEntryPoint = std::wstring(desc.EntryPoint.begin(), desc.EntryPoint.end());
    std::wstring_view wTargetProfile = GetTargetProfile(desc.ShaderModel, desc.ShaderType);

    std::vector<DxcDefine> dxcDefines;
    dxcDefines.reserve(desc.Defines.size());
    for (const auto& [name, value] : desc.Defines)
    {
        dxcDefines.emplace_back(name.data(), value.data());
    }

    CompilationResult result = CompileInternal(wFilepath, wEntryPoint, wTargetProfile, dxcDefines, flags);
    return NebShader(desc.ShaderType, result.Binary, result.Pdb, result.Reflection);
}

std::wstring_view NebShaderCompiler::GetTargetProfile(EShaderModel shaderModel, EShaderType shaderType) const
{
    switch (shaderModel)
    {
    case EShaderModel::sm_6_5:
    {
        switch (shaderType)
        {
        case EShaderType::Vertex: return L"vs_6_5";
        case EShaderType::Amplification: return L"as_6_5";
        case EShaderType::Mesh: return L"ms_6_5";
        case EShaderType::Pixel: return L"ps_6_5";
        case EShaderType::Compute: return L"cs_6_5";
        default: ThrowIfFalse(false); return L"";
        };
    }; break;
    default: ThrowIfFalse(false); return L"";
    };
}

NebShaderCompiler::CompilationResult NebShaderCompiler::CompileInternal(
    std::wstring_view filepath,
    std::wstring_view entryPoint,
    std::wstring_view targetProfile,
    const std::vector<DxcDefine>& defines, EShaderCompilationFlags flags)
{
    // https://strontic.github.io/xcyclopedia/library/dxc.exe-0C1709D4E1787E3EB3E6A35C85714824.html
    static constexpr std::array dxcDefault =
    {
#ifdef WARP_DEBUG
            L"-Od", // no optimization
#else
            L"-O3", // max optimization
#endif
            L"-Zi", // enable debug information
            L"-WX", // treat warnings as errors
            L"-Zpr", // enforce row-major ordering

            // Use the /all_resources_bound / D3DCOMPILE_ALL_RESOURCES_BOUND compile flag if possible
            // This allows for the compiler to do a better job at optimizing texture accesses.We have seen frame rate improvements of > 1 % when toggling this flag on.
            L"-all_resources_bound", // Enables agressive flattening
    };

    std::wstring wPdbPath = std::filesystem::path(filepath)
        .replace_extension("pdb")
        .wstring();

    std::vector<LPCWSTR> dxcArguments(dxcDefault.begin(), dxcDefault.end());
    if (flags & eShaderCompilationFlag_StripDebug)
    {
        // If we want to strip PDB, we will write into a binary file later
        dxcArguments.push_back(L"-Qstrip_debug");

        // TODO: I wonder why -Fd flag won't write into the file on its own?
        // Write debug information to the given file, or automatically named file in directory when ending in '\'
        dxcArguments.push_back(L"-Fd");
        dxcArguments.push_back(wPdbPath.c_str());
    }
    else dxcArguments.push_back(L"-Qembed_debug"); // If we do not strip, we embed it

    if (flags & eShaderCompilationFlag_StripReflect)
        dxcArguments.push_back(L"-Qstrip_reflect");

    ComPtr<IDxcCompilerArgs> compilerArgs;
    ThrowIfFailed(m_DxcUtils->BuildArguments(
        filepath.data(),
        entryPoint.data(),
        targetProfile.data(),
        dxcArguments.data(), static_cast<UINT32>(dxcArguments.size()),
        defines.data(), static_cast<UINT32>(defines.size()),
        compilerArgs.GetAddressOf())
    );

    CompilationResult result{};
    ComPtr<IDxcBlobEncoding> srcBlobEncoding;
    HRESULT hr = m_DxcUtils->LoadFile(filepath.data(), 0, srcBlobEncoding.GetAddressOf());
    if (FAILED(hr))
    {
        //WARP_LOG_ERROR("[CShaderCompiler] Failed to load a shader at location {}. Compilation process will be aborted...", strFilepath);
        return result;
    }

    const DxcBuffer srcBuffer = {
        .Ptr = srcBlobEncoding->GetBufferPointer(),
        .Size = srcBlobEncoding->GetBufferSize(),
        .Encoding = 0
    };

    ComPtr<IDxcResult> compilationResult;
    hr = m_DxcCompiler->Compile(&srcBuffer,
        compilerArgs->GetArguments(),
        compilerArgs->GetCount(),
        m_DxcIncludeHandler.Get(),
        IID_PPV_ARGS(compilationResult.GetAddressOf()));
    if (FAILED(hr))
    {
        //WARP_LOG_ERROR("[CShaderCompiler] Internal compilation error of a shader at location {}. Aborting...", strFilepath);
        //WARP_LOG_ERROR("[CShaderCompiler] This might be related with E_INVALID_ARGS error");
        return result;
    }

    HRESULT status;
    ThrowIfFailed(compilationResult->GetStatus(&status));
    if (FAILED(status))
    {
        ComPtr<IDxcBlobUtf8> errorBlob;
        ThrowIfFailed(compilationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(errorBlob.GetAddressOf()), nullptr));
        //WARP_LOG_ERROR("[CShaderCompiler] Failed to compile a shader: {}", (char*)errorBlob->GetStringPointer());
        return result; // return empty result;
    }

    ThrowIfFailed(compilationResult->GetResult(result.Binary.GetAddressOf()));

    if (compilationResult->HasOutput(DXC_OUT_PDB))
    {
        ThrowIfFailed(compilationResult->GetOutput(
            DXC_OUT_PDB,
            IID_PPV_ARGS(result.Pdb.GetAddressOf()),
            nullptr));

        if (flags & eShaderCompilationFlag_StripDebug)
        {
            std::ofstream pdbFile = std::ofstream(wPdbPath, std::ios::binary);
            pdbFile.write((const char*)result.Pdb->GetBufferPointer(), result.Pdb->GetBufferSize());
            pdbFile.close();
        }
    }

    if (flags & eShaderCompilationFlag_StripReflect)
    {
        ThrowIfFailed(
            compilationResult->GetOutput(
                DXC_OUT_REFLECTION,
                IID_PPV_ARGS(result.Reflection.GetAddressOf()),
                nullptr)
        );
    }

    return result;
}
