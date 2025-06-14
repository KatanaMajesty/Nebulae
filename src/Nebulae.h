#pragma once

#include "common/TimeWatch.h"
#include "core/Scene.h"
#include "core/GLTFSceneImporter.h"
#include "Renderer.h"
#include "Raytracer.h"
#include "util/ScopedPointer.h"

// TODO: VERY TEMP, just to switch between raytracing and plain raster
#include "input/Keyboard.h"

namespace Neb
{

    struct AppSpec
    {
        HWND Handle = NULL;
        std::filesystem::path AssetsDirectory;
    };

    class Nebulae
    {
    private:
        Nebulae() = default;

    public:
        Nebulae(const Nebulae&) = delete;
        Nebulae& operator=(const Nebulae&) = delete;

        static Nebulae& Get();

        bool Init(const AppSpec& appSpec);
        bool IsInitialized() const { return m_isInitialized; }

        // sync with remaining work
        void Shutdown();

        // TODO: Currently render just takes the first imported scene in GLTFSceneImporter, if any
        // this behavior should be expanded upon by allowing more control on the API, but the idea should
        // still be the same
        void Render();
        void Resize(UINT width, UINT height);

        const AppSpec& GetSpecification() const { return m_appSpec; }

        GLTFSceneImporter* GetSceneImporter() { return m_sceneImporter; }
        const GLTFSceneImporter* GetSceneImporter() const { return m_sceneImporter; }

        Renderer* GetRenderer() { return m_renderer; }
        const Renderer* GetRenderer() const { return m_renderer; }

        void OnKeyInteraction(const KeyboardEvent_KeyInteraction& event);

    private:
        bool m_isInitialized = false;
        AppSpec m_appSpec = {};

        TimeWatch m_timeWatch;
        SecondsF32 m_lastFrameSeconds = SecondsF32(0.0f);

        Scoped<GLTFSceneImporter> m_sceneImporter;
        Scoped<Renderer> m_renderer;
    };

}