#pragma once

#include <filesystem>
#include <vector>
#include <memory>
#include <TinyGLTF/tiny_gltf.h>

#include "GLTFScene.h"

namespace Neb
{

    class GLTFSceneImporter
    {
    public:
        bool ImportScenesFromFile(const std::filesystem::path& filepath);
        void Clear();

        std::vector<std::unique_ptr<GLTFScene>> ImportedScenes;
        tinygltf::TinyGLTF GLTFLoader;
        tinygltf::Model GLTFModel;
    
    private:
        // return false if failed to import scene. In such case, the entire scene will be discarded and
        // some warning will be logged
        bool ImportScene(GLTFScene* scene, tinygltf::Scene& src);
    };

}