#pragma once

#include <filesystem>
#include <vector>
#include <memory>
#include <TinyGLTF/tiny_gltf.h>

#include "Scene.h"
#include "../nri/stdafx.h"
#include "../nri/Manager.h"
#include "../nri/DescriptorAllocation.h"

namespace Neb
{
    
    // Quickstart with glTF https://www.khronos.org/files/gltf20-reference-guide.pdf
    // Afterwards we chill here - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

    class GLTFSceneImporter
    {
    public:
        GLTFSceneImporter() = default;

        bool ImportScenesFromFile(const std::filesystem::path& filepath);
        void Clear();

        // Maybe make them private? Dont really care now
        std::vector<std::unique_ptr<Scene>> ImportedScenes;
    
    private:
        // return false if failed to import scene. In such case, the entire scene will be discarded and
        // some warning will be logged
        bool ImportScene(Scene* scene, tinygltf::Scene& src);

        // We want to immediately convert all the images of the scene to D3D12 resources
        // so that we avoid lazy-loading them as well as loading them more than once
        bool SubmitD3D12Resources();
        bool SubmitD3D12Images();
        bool SubmitD3D12Buffers();
        void WaitD3D12ResourcesOnCopyQueue();

        bool IsTangentPostprocessingNeeded();
        bool SubmitPostprocessingD3D12Resources();
        bool SubmitTangentPostprocessingD3D12Buffer();

        // Node processing
        bool ImportGLTFNode(Scene* scene, tinygltf::Scene& src, int32_t nodeID);
        bool ImportStaticMesh(nri::StaticMesh& mesh, tinygltf::Mesh& src);
        
        nri::D3D12Rc<ID3D12Resource> GetTextureFromGLTFScene(int32_t index);
        void InitMaterialTextureDescriptor(ID3D12Resource* resource, nri::EMaterialTextureType type, const nri::DescriptorRange& range);

        tinygltf::TinyGLTF m_GLTFLoader;
        tinygltf::Model m_GLTFModel;

        std::vector<nri::D3D12Rc<ID3D12Resource>> m_GLTFTextures;
        std::vector<nri::D3D12Rc<ID3D12Resource>> m_GLTFBuffers;
        std::vector<nri::D3D12Rc<ID3D12Resource>> m_stagingBuffers; // upload resources that are currently being used
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_stagingCommandList;
    };

}