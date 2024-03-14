#include "RayTracer.h"

RayTracer::RayTracer(HWND hwnd)
    : m_NRIManager(NRICreationDesc{
            .Handle = hwnd
        })
{
}

void RayTracer::Render()
{
}
