#pragma once

#include "Scene.h"
#include "../nri/stdafx.h"
#include "../nri/DescriptorHeapAllocation.h"
#include "../nri/CommandAllocatorPool.h"
#include "../util/ScopedPointer.h"

#include <TinyGLTF/tiny_gltf.h>

#include <filesystem>
#include <vector>
#include <memory>

namespace Neb
{

    // Quickstart with glTF https://www.khronos.org/files/gltf20-reference-guide.pdf
    // Afterwards we chill here - https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

    enum class EGLTFType
    {
        AsciiFile,
        Binary
    };

    class GLTFSceneImporter
    {
    public:
        GLTFSceneImporter();

        bool ImportScenesFromFile(const std::filesystem::path& filepath, EGLTFType type = EGLTFType::AsciiFile);
        void Clear();

        // Maybe make them private? Dont really care now
        std::vector<Scoped<Scene>> ImportedScenes;

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
        bool ImportStaticMesh(nri::StaticMesh& mesh, tinygltf::Mesh& src, AABB* pUpdatedSceneAABB);

        Mat4 GetTransformationMatrix(tinygltf::Node& node);

        nri::D3D12Rc<ID3D12Resource> GetTextureFromGLTFScene(int32_t index);
        void InitMaterialTextureDescriptor(ID3D12Resource* resource, nri::EMaterialTextureType type, const nri::DescriptorHeapAllocation& heapAllocation);

        tinygltf::TinyGLTF m_GLTFLoader;
        tinygltf::Model m_GLTFModel;

        std::vector<nri::D3D12Rc<ID3D12Resource>> m_GLTFTextures;
        std::vector<nri::D3D12Rc<ID3D12Resource>> m_GLTFBuffers;
        std::vector<nri::D3D12Rc<ID3D12Resource>> m_stagingBuffers; // upload resources that are currently being used

        // TODO: I am not sure how to handle this best, but I think using own command allocators and fences here
        // would make it more sustainable + we could handle renderer waiting on assets better on outer layer in Nebulae easier
        nri::D3D12Rc<ID3D12GraphicsCommandList> m_stagingCommandList;
        nri::D3D12Rc<ID3D12Fence> m_copyFence;
        UINT64 m_copyFenceValue = 0;
    };

}