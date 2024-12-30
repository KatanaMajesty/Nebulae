#include "Nebulae.h"

#include "common/Assert.h"
#include "common/Log.h"
#include "input/InputManager.h"
#include "nri/Device.h"

namespace Neb
{

    Nebulae& Nebulae::Get()
    {
        static Nebulae instance;
        return instance;
    }

    bool Nebulae::Init(const AppSpec& appSpec)
    {
        m_appSpec = appSpec;

        // Firstly initialize rendering interface manager
        // it is now singleton, annoying to manage it all the time
        nri::NRIDevice& device = nri::NRIDevice::Get();

        m_renderer = MakeScoped<Renderer>();
        if (!m_renderer->Init(appSpec.Handle))
        {
            NEB_ASSERT(false, "Failed to initialize renderer");
            NEB_LOG_ERROR("Nebulae -> Failed to initialize renderer");
            return false;
        }

        m_sceneImporter = MakeScoped<GLTFSceneImporter>();
        nri::ThrowIfFalse(m_sceneImporter->ImportScenesFromFile(appSpec.AssetsDirectory / "DamagedHelmet" / "DamagedHelmet.gltf"));
        // nri::ThrowIfFalse(m_sceneImporter.ImportScenesFromFile(appSpec.AssetsDirectory / "Sponza" / "Sponza.gltf"));

        // TODO: This is currently hardcoded as we know that very first scene will be used for rendering, thus we register its callbacks
        Neb::Scene* scene = m_sceneImporter->ImportedScenes.front().get();
        Neb::Mouse& mouse = Neb::InputManager::Get().GetMouse();
        {
            mouse.RegisterCallback<Neb::MouseEvent_Scrolled>(&Neb::Scene::OnMouseScroll, scene);
            mouse.RegisterCallback<Neb::MouseEvent_CursorHotspotChanged>(&Neb::Scene::OnMouseCursorMoved, scene);
            mouse.RegisterCallback<Neb::MouseEvent_ButtonInteraction>(&Neb::Scene::OnMouseButtonInteract, scene);
        }
        Neb::Keyboard& keyboard = Neb::InputManager::Get().GetKeyboard();
        {
            keyboard.RegisterCallback<Neb::KeyboardEvent_KeyInteraction>(&Neb::Nebulae::OnKeyInteraction, this);
        }

        if (!m_renderer->InitSceneContext(scene))
        {
            NEB_ASSERT(false, "Failed to initialize ray traced scene");
            NEB_LOG_ERROR("Nebulae -> Failed to init ray traced scene");
            return false;
        }

        // At the very end begin the time watch
        m_timeWatch.Begin();
        m_isInitialized = true;
        return m_isInitialized;
    }

    void Nebulae::Shutdown()
    {
        m_sceneImporter.Release();
        m_renderer.Release();
    }

    void Nebulae::Render()
    {
        NEB_ASSERT(IsInitialized(), "Nebulae is not initialized");

        SecondsF32 elapsed = m_timeWatch.Elapsed<SecondsF32>();
        const float timestep = (elapsed - m_lastFrameSeconds).count();
        const float framerate = 1.0f / timestep;
        m_lastFrameSeconds = elapsed;

        static float secondsSinceLastFps = 0.0f;
        if (secondsSinceLastFps > 1.0f)
        {
            secondsSinceLastFps = 0.0f;
            NEB_LOG_INFO("Frametime is {:.1f}ms ({:.1f} fps)", timestep * 1000.0f, framerate);
        }

        secondsSinceLastFps += timestep;

        (m_isRaytracer ? m_renderer->RenderSceneRaytraced(timestep) : m_renderer->RenderScene(timestep));
    }

    void Nebulae::Resize(UINT width, UINT height)
    {
        NEB_ASSERT(IsInitialized(), "Nebulae is not initialized");
        m_renderer->Resize(width, height);
    }

    void Nebulae::OnKeyInteraction(const KeyboardEvent_KeyInteraction& event)
    {
        if (event.NextState == eKeycodeState_Pressed)
        {
            switch (event.Keycode)
            {
            case eKeycode_Esc: 
                nri::ThrowIfFalse(DestroyWindow(m_appSpec.Handle), "Could not properly destroy window on escape!"); 
                break;
            case eKeycode_F1: m_isRaytracer = !m_isRaytracer; break;
            }
        }
    }

} // Neb namespace