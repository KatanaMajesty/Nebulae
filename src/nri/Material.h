#pragma once

#include "stdafx.h"
#include "../core/Math.h"

namespace Neb::nri
{

    // Defines the entire material itself. Material definition based upon glTF 2.0 spec
    // For more info: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-material-pbrmetallicroughness
    // 
    // We only support metallic-roughness PBR materials
    struct Material
    {
        D3D12Rc<ID3D12Resource> AlbedoMap;
        D3D12Rc<ID3D12Resource> NormalMap;
        // RGBA texture, B for metalness, G for roughness (R, A channels ignored)
        D3D12Rc<ID3D12Resource> RoughnessMetalnessMap;

        // Material factors, act as texture replacements. 
        // If D3D12Rc of a respective texture is null - use them
        Neb::Vec4 AlbedoFactor = Neb::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        Neb::Vec2 RoughnessMetalnessFactor = Neb::Vec2(1.0f, 0.0f);
    };

}