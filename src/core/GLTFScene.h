#pragma once

#include <vector>
#include "../nri/StaticMesh.h"

namespace Neb
{

    struct GLTFScene
    {
        std::vector<nri::StaticMesh> StaticMeshes;
    };

} // Neb namespace