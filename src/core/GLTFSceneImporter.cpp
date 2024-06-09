#include "GLTFSceneImporter.h"

#include "../common/Defines.h"
#include "../common/Log.h"

namespace Neb
{

    bool GLTFSceneImporter::ImportScenesFromFile(const std::filesystem::path& filepath)
    {
        Clear(); // cleanup before work
        std::string err, warn;

        if (!GLTFLoader.LoadASCIIFromFile(&GLTFModel, &err, &warn, filepath.string()))
        {
            NEB_ASSERT(!err.empty());
            NEB_LOG_ERROR("{}", err);
            return false;
        }

        if (!warn.empty())
        {
            NEB_LOG_WARN("{}", warn);
        }

        for (tinygltf::Scene& src : GLTFModel.scenes)
        {
            std::unique_ptr<GLTFScene> scene = std::make_unique<GLTFScene>();
            if (ImportScene(scene.get(), src))
            {
                // If successfully imported - move the scene to the list of imported ones,
                // otherwise just discard
                ImportedScenes.push_back(std::move(scene));
            }
        }

        return true;
    }

    void GLTFSceneImporter::Clear()
    {
        ImportedScenes.clear();
        GLTFLoader = tinygltf::TinyGLTF(); // just in case clean it up as well
        GLTFModel = tinygltf::Model(); // destroy this as well
    }

    bool GLTFSceneImporter::ImportScene(GLTFScene* scene, tinygltf::Scene& src)
    {
        return false;
    }

} // Neb namespace