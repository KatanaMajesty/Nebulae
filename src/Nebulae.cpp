#include "Nebulae.h"

#include "common/Defines.h"
#include "common/Log.h"

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
        nri::Manager& nriManager = nri::Manager::Get();

        if (!m_renderer.Init(appSpec.Handle))
        {
            NEB_LOG_ERROR("Nebulae -> Failed to initialize renderer");
            NEB_ASSERT(false);
            return false;
        }

        m_sceneImporter.Clear();

        // At the very end begin the time watch
        m_timeWatch.Begin();
        m_isInitialized = true;
        return m_isInitialized;
    }

    void Nebulae::Render()
    {
        NEB_ASSERT(IsInitialized());
        if (m_sceneImporter.ImportedScenes.empty())
            return;

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

        Scene* scene = m_sceneImporter.ImportedScenes.front().get();
        m_renderer.RenderScene(timestep, scene);
    }

    void Nebulae::Resize(UINT width, UINT height)
    {
        NEB_ASSERT(IsInitialized());
        m_renderer.Resize(width, height);
    }

    void Nebulae::OnKeyInteraction(const KeyboardEvent_KeyInteraction& event)
    {
        if (event.NextState == eKeycodeState_Pressed)
        {
            switch (event.Keycode)
            {
            case eKeycode_Esc: PostQuitMessage(0); break;
            }
        }
    }

} // Neb namespace