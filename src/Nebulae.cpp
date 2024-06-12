#include "Nebulae.h"

#include "common/Defines.h"

namespace Neb
{

    BOOL Nebulae::Init(HWND hwnd)
    {
        // Firstly initialize rendering interface manager
        m_nriManager.Init();

        // Now initialize the swapchain
        if (!m_swapchain.Init(hwnd, &m_nriManager))
        {
            NEB_ASSERT(false); // failed to initialize the swapchain
            return FALSE;
        }

        m_sceneImporter = GLTFSceneImporter(&m_nriManager);
        return TRUE;
    }

    void Nebulae::Resize(UINT width, UINT height)
    {
        // Handle the return result better
        m_swapchain.Resize(width, height);
    }

    void Nebulae::Render()
    {
    }

} // Neb namespace