#pragma once

#include "core/Scene.h"

namespace Neb
{

    class Raytracer
    {
    public:
        void InitForScene();
        void RenderScene(Scene* scene);
    };

} // Neb namespace