#include "RTPipeline.h"

#include "common/Assert.h"
#include "util/Types.h"

namespace Neb::nri
{

    //void RTPipeline::AddLibrary(const Shader& shader)
    //{
    //    NEB_ASSERT(shader.HasBinary() && shader.GetType() == EShaderType::Library,
    //        "Only valid ray-tracing library modules are allowed");
    //}

    //void RTPipeline::Init(Rc<ID3D12Device5> device, const Specification& spec)
    //{
    //    NEB_LOG_WARN_IF(m_pso != nullptr, "PSO is already initialized for this RTPipeline object");

    //    // TODO: Precalculate size of subobject array
    //    std::vector<D3D12_STATE_SUBOBJECT> subobjectArray;

    //    D3D12_STATE_OBJECT_CONFIG config = {}; // just some flags for local/external defines? Not sure whats that
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG, .pDesc = &config });

    //    // The presence of this subobject in a state object is optional.
    //    D3D12_GLOBAL_ROOT_SIGNATURE globalRS = { .pGlobalRootSignature = spec.globalRS.Get() };
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, .pDesc = &globalRS });

    //    // IMPORTANT! On some versions of the DirectX Runtime NodeMask of 0 will be incorrectly handled
    //    // which will lead to errors when attempting to use the state object later
    //    D3D12_NODE_MASK nodeMask = { .NodeMask = 1 };
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK, .pDesc = &nodeMask });

    //    // !!! DXIL Exports part !!! -- Begin

    //    // Gather some metadata about DXIL exports
    //    UINT numDXILExports = static_cast<UINT>(spec.libraries.size()); // DXIL libraries should match the number of libraries provided in spec

    //    // we need to keep all local descriptions alive in inner loops/scoped too correctly compile pipeline state
    //    // so we just keep arrays of whatever we need here
    //    // TODO: Maybe move them elsewhere
    //    std::vector<D3D12_DXIL_LIBRARY_DESC> libraryDescArray = {};
    //    libraryDescArray.reserve(numDXILExports); 

    //    struct InternalExport
    //    {
    //        std::vector<D3D12_EXPORT_DESC> exportedSymbols;
    //    };

    //    // we provide std::span<std::wstring_view>, but we need pointer to LPCWSTR array
    //    // thus store it here, the number of export descs will be at least the number of DXIL libraries
    //    std::vector<InternalExport> exportDescArray;
    //    exportDescArray.reserve(numDXILExports);

    //    {
    //        // process DXIL exports of each library specified. In-place handle all exports, mappings, root signatures
    //        static_assert(sizeof(D3D12_LOCAL_ROOT_SIGNATURE) == sizeof(ID3D12RootSignature*));

    //        for (const ShaderLibrary& library : spec.libraries)
    //        {
    //            Shader shader = library.shader;
    //            NEB_ASSERT(EnumValue(shader.GetModel()) > EnumValue(EShaderModel::sm_6_3) && shader.GetType() == EShaderType::Library,
    //                "DXIL library must have been compiled with library target 6.3 or higher");

    //            // Convert libraries' exported symbols to internal exports
    //            InternalExport& internalExport = exportDescArray.emplace_back();
    //            internalExport.exportedSymbols.reserve(library.exportedSymbols.size());
    //            for (std::wstring_view exportedSymbol : library.exportedSymbols)
    //            {
    //                // The name to be exported. If the name refers to a function that is overloaded, 
    //                // a modified version of the name (e.g. encoding function parameter information in name string) can be provided to disambiguate which overload to use. 
    //                // The modified name for a function can be retrieved using HLSL compiler reflection.
    //                //
    //                // for now dont use ExportToRename
    //                D3D12_EXPORT_DESC& exportDesc = internalExport.exportedSymbols.emplace_back();
    //                exportDesc.Name = exportedSymbol.data();
    //                exportDesc.ExportToRename = nullptr;
    //                exportDesc.Flags = D3D12_EXPORT_FLAG_NONE; // reserved
    //            }

    //            D3D12_DXIL_LIBRARY_DESC& libraryDesc = libraryDescArray.emplace_back();
    //            libraryDesc.DXILLibrary = shader.GetBinaryBytecode();
    //            libraryDesc.NumExports = static_cast<UINT>(internalExport.exportedSymbols.size());
    //            libraryDesc.pExports = internalExport.exportedSymbols.data();

    //            subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, .pDesc = &libraryDesc });
    //        }
    //    }

    //    // !!! DXIL Exports part !!! -- End
    //    

    //    

    //    D3D12_EXISTING_COLLECTION_DESC existingCollection = {}; // TODO: Is it used?
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION, .pDesc = &existingCollection });

    //    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION exportAssociation = {}; // should be a mapping of local RS and their respective shader export
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, .pDesc = &exportAssociation });

    //    D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION dxilExportAssociation = {};
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION, .pDesc = &dxilExportAssociation });

    //    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, .pDesc = &shaderConfig });

    //    D3D12_HIT_GROUP_DESC hitGroup = {}; // Should also match the number of hitgroups
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, .pDesc = &hitGroup });

    //    // TODO: Add check if raytracing 1.1 features are supported
    //    //       I mean clarify if that even defines the difference between config/config1 versions
    //    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipelineConfig1 = {}; // Adds flags to skip triangles or procedural primitives, might be useful?
    //    subobjectArray.emplace_back(D3D12_STATE_SUBOBJECT{ .Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, .pDesc = &pipelineConfig1 });

    //    D3D12_STATE_OBJECT_DESC stateDesc = {};
    //    stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    //    stateDesc.NumSubobjects = static_cast<UINT>(subobjectArray.size());
    //    stateDesc.pSubobjects = subobjectArray.data();
    //    device->CreateStateObject(&stateDesc, IID_PPV_ARGS(m_pso.ReleaseAndGetAddressOf()));
    //}

    //void RTPipelineBuilder::AddLibrary(const RTPSOLibrary& library)
    //{
    //    Shader shader = library.shaderLibrary;
    //    NEB_ASSERT(EnumValue(shader.GetModel()) > EnumValue(EShaderModel::sm_6_3) && shader.GetType() == EShaderType::Library,
    //        "DXIL library must have been compiled with library target 6.3 or higher");

    //    UINT numExports = static_cast<UINT>(library.symbols.size());
    //    UINT prevOffset = static_cast<UINT>(m_exportDescArray.size());

    //    // Get pointer to the beginning of exports for this library
    //    m_exportDescArray.resize(m_exportDescArray.size() + numExports);
    //    D3D12_EXPORT_DESC* exports = m_exportDescArray.data() + prevOffset;
    //    for (std::wstring_view symbol : library.symbols)
    //    {
    //        // The name to be exported. If the name refers to a function that is overloaded,
    //        // a modified version of the name (e.g. encoding function parameter information in name string) can be provided to disambiguate which overload to use.
    //        // The modified name for a function can be retrieved using HLSL compiler reflection.
    //        //
    //        // for now dont use ExportToRename
    //        m_exportDescArray.push_back(D3D12_EXPORT_DESC{
    //            .Name = symbol.data(),
    //            .ExportToRename = nullptr,
    //            .Flags = D3D12_EXPORT_FLAG_NONE // reserved
    //        });
    //    }
    //}

    void RTPipelineConstructor::AddShaderLibrary(const RTShaderLibrary& library)
    {
        //const Shader& shader = library.shader;
        //NEB_ASSERT(EnumValue(shader.GetModel()) > EnumValue(EShaderModel::sm_6_3) && shader.GetType() == EShaderType::Library,
        //    "DXIL library must have been compiled with library target 6.3 or higher");

        //// Convert libraries' exported symbols to internal exports
        //RTShaderLibrary& internalExport = m_shaderLibraries.emplace_back();
        //internalExport.symbols.reserve(library.symbols.size());

        //for (std::wstring_view exportedSymbol : library.symbols)
        //{
        //    // The name to be exported. If the name refers to a function that is overloaded,
        //    // a modified version of the name (e.g. encoding function parameter information in name string) can be provided to disambiguate which overload to use.
        //    // The modified name for a function can be retrieved using HLSL compiler reflection.
        //    //
        //    // for now dont use ExportToRename
        //    RT& exportDesc = internalExport.symbols.emplace_back();
        //    exportDesc.Name = exportedSymbol.data();
        //    exportDesc.ExportToRename = nullptr;
        //    exportDesc.Flags = D3D12_EXPORT_FLAG_NONE; // reserved
        //}

        //D3D12_DXIL_LIBRARY_DESC& libraryDesc = internalExport.libraryDesc;
        //libraryDesc.DXILLibrary = shader.GetBinaryBytecode();
        //libraryDesc.NumExports = static_cast<UINT>(internalExport.exportDescArray.size());
        //libraryDesc.pExports = internalExport.exportDescArray.data();
    }

    RTPipelineConstructor::LibraryConstructionOutput RTPipelineConstructor::ConstructLibraries()
    {
        LibraryConstructionOutput output;
        output.units.reserve(m_shaderLibraries.size()); // avoid resizes at all costs to keep raw pointers alive

        for (const RTShaderLibrary& library : m_shaderLibraries)
        {
            LibraryConstructionUnit& unit = output.units.emplace_back();
            unit.exportDescArray.reserve(library.symbols.size()); // reserve size to avoid allocations
            
            for (std::wstring_view exportedSymbol : library.symbols)
            {
                // The name to be exported. If the name refers to a function that is overloaded,
                // a modified version of the name (e.g. encoding function parameter information in name string) can be provided to disambiguate which overload to use.
                // The modified name for a function can be retrieved using HLSL compiler reflection.
                //
                // for now dont use ExportToRename
                unit.exportDescArray.push_back(D3D12_EXPORT_DESC{
                    .Name = exportedSymbol.data(),
                    .ExportToRename = nullptr,
                    .Flags = D3D12_EXPORT_FLAG_NONE, // reserved
                });
            }

            unit.libraryDesc = {};
            unit.libraryDesc.DXILLibrary = library.shader.GetBinaryBytecode();
            unit.libraryDesc.NumExports = static_cast<UINT>(unit.exportDescArray.size());
            unit.libraryDesc.pExports = unit.exportDescArray.data();
        }

        return output;
    }

    RTPipelineObject RTPipelineConstructor::Construct()
    {
        /*D3D12_STATE_OBJECT_DESC stateDesc = {};
        stateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateDesc.NumSubobjects = static_cast<UINT>(subobjectArray.size());
        stateDesc.pSubobjects = subobjectArray.data();
        m_device->CreateStateObject(&stateDesc, IID_PPV_ARGS(m_pso.ReleaseAndGetAddressOf()));*/
        NEB_ASSERT(false, "Not implemented");
        return RTPipelineObject();
    }

} // Neb::nri namespace