#include "Nebulae.h"

namespace Neb
{

    Nebulae::Nebulae(HWND hwnd)
        : m_nriManager(nri::ManagerDesc{ .Handle = hwnd })
    {
    }

    void Nebulae::Render()
    {
    }

} // Neb namespace