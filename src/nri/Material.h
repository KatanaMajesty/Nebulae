#pragma once

#include "stdafx.h"
#include "../core/Math.h"
#include "DescriptorAllocation.h"

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

        // We utilize null descriptors here
        // https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#null-descriptors
        //
        // The idea is to store descriptors for each material texture in one descriptor range
        // If material dont have a specific texture then null descriptor will be in place, thats it
        
    };

}