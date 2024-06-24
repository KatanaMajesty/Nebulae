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

        int64_t elapsedMillis = m_timeWatch.Elapsed();
        int64_t timestepMillis = elapsedMillis - m_lastFrameMillis;
        m_lastFrameMillis = elapsedMillis;
        float timestep = timestepMillis * 0.001f;
        float framerate = 1.0f / timestep;

        static float secondsSinceLastFps = 0.0f;
        if (secondsSinceLastFps > 1.0f)
        {
            secondsSinceLastFps = 0.0f;
            NEB_LOG_INFO("framerate is {}", framerate);
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

} // Neb namespace