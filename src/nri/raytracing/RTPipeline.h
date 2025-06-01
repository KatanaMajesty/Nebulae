#pragma once

#include "nri/Shader.h"
#include "nri/stdafx.h"

#include <vector> // TODO: move to cpp?

#include <span>
#include <string_view>

namespace Neb::nri
{

    // In DXR hit-related shaders are grouped into hit-groups
    // Each hit-group can include closestHit, anyHit and intersection shaders (but optional)
    // 
    // -    The intersection shader, which can be used to intersect custom geometry, and is called upon
    //      hitting the bounding box the the object. A default one exists to intersect triangles
    // -    The any hit shader, called on each intersection, which can be used to perform early
    //      alpha-testing and allow the ray to continue if needed. Default is a pass-through.
    // -    The closest hit shader, invoked on the hit point closest to the ray start.
    // 
    // The shaders in a hit group share the same root signature, and are only referred to by the
    // hit group name (name) in other places of the program.
    struct RTPSOHitgroup
    {
        std::wstring_view name;

        std::wstring_view closestHitSymbol;
        std::wstring_view anyHitSymbol;
        std::wstring_view intersectionSymbol;
    };

    // Shaders and hit groups may have various root signatures. This call associates a root
    // signature to one or more symbols. 
    // 
    // All imported symbols must be associated to one root signature.
    struct RTRootSignatureAssociation
    {
        ID3D12RootSignature* rs;
        std::span<std::wstring_view> name;
    };

    // Represents an association object of local root signature to one or more exported symbols
    // symbols should either be entry point names or hit-group names
    struct RTPSOLocalRSAssociation
    {
        Rc<ID3D12RootSignature> localRS;
        std::span<std::wstring_view> symbols;
    };

    struct RTShaderLibrary
    {
        Shader shader;
        std::vector<std::wstring_view> symbols; // exported symbols
    };

    // https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#state-objects
    // Intended to be used as a lightweight proxy for RT PSO contruction for DXR
    class RTPipelineConstructor
    {
    public:
        RTPipelineConstructor(ID3D12Device5* device)
            : m_device(device)
        {
        }

        void SetGlobalRS(const ID3D12RootSignature* globalRS) { m_globalRS = globalRS; }

        // Add a DXIL library to the pipeline. Note that this library has to be
        // compiled with dxc, using a lib_6_3 target. The exported symbols must correspond exactly to the
        // names of the shaders declared in the library, although unused ones can be omitted.
        void AddShaderLibrary(const RTShaderLibrary& library);
        
        // constructs a pipeline object based on context of the constructor
        RTPipelineObject Construct();

    private:
        ID3D12Device5* m_device = nullptr;

        Rc<ID3D12RootSignature> m_globalRS;

        // relies on lifetime of export symbols from caller
        std::vector<RTShaderLibrary> m_shaderLibraries;
        struct LibraryConstructionUnit
        {
            std::vector<D3D12_EXPORT_DESC> exportDescArray;
            D3D12_DXIL_LIBRARY_DESC libraryDesc;
        };
        struct LibraryConstructionOutput { std::vector<LibraryConstructionUnit> units; };
        LibraryConstructionOutput ConstructLibraries(); // constructs shader libraries
    };

    class RTPipelineObject
    {
    public:
        RTPipelineObject() = default;
        RTPipelineObject(Rc<ID3D12StateObject> pso, Rc<ID3D12StateObjectProperties> psoProperties = nullptr)
            : m_pso(pso)
            , m_psoProperties(psoProperties)
        {
        }

        RTPipelineObject(const RTPipelineObject&) = default;
        RTPipelineObject& operator=(const RTPipelineObject&) = default;

        ID3D12StateObject* GetD3D12Pso() const { return m_pso.Get(); }
        ID3D12StateObjectProperties* GetD3D12PsoProperties() const { return m_psoProperties.Get(); }

    private:
        Rc<ID3D12StateObject> m_pso;
        Rc<ID3D12StateObjectProperties> m_psoProperties;
    };

} // Neb::nri namespace