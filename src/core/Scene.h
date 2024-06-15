#pragma once

#include <vector>
#include "../nri/StaticMesh.h"

namespace Neb
{

    struct Scene
    {
        std::vector<nri::StaticMesh> StaticMeshes;
    };

} // Neb namespace