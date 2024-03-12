#pragma once

#include "NRIManager.h"

class RayTracer
{
public:
    RayTracer(HWND hwnd);

    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    RayTracer(RayTracer&&) = delete;
    RayTracer& operator=(RayTracer&&) = delete;

private:
    NRIManager m_NRIManager;
};