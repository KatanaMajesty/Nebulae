#pragma once

#include <d3d12.h> // Include before SimpleMath to have interop between D3D12 and the library
#include "DirectXTK12/Include/SimpleMath.h"

namespace Neb
{

    // Insane wrapper of https://github.com/microsoft/DirectXTK/wiki/SimpleMath
    // SimpleMath wastes a bit of runtime performance compared to raw DirectXMath, so be aware
    //
    // SimpleMath as with DirectXMath uses row-major ordering for matrices. 
    // This means that elements are stored in memory in the following order: _11, _12, _13, _14, _21, _22, etc.
    //
    // With the built-in Effects this is done internally, but if writing your own shaders and managing your own constant buffers, 
    // you will need to ensure you pass in your matrix data in the correct order for your HLSL shader settings. 
    // This means sticking with the HLSL default by transposing your matrices as you update the constant buffer, 
    // using #pragma pack_matrix(row_major) in your HLSL shader source, or compiling your shaders with /Zpr.
    //
    // As of now Nebulae uses /Zpr flag, but will probably deprecate that in favor of faster column-major order

    // Vectors
    using Vec2 = DirectX::SimpleMath::Vector2;
    using Vec3 = DirectX::SimpleMath::Vector3;
    using Vec4 = DirectX::SimpleMath::Vector4;

    // Matrices
    using Mat4 = DirectX::SimpleMath::Matrix;

    // Helpers
    using Rect = DirectX::SimpleMath::Rectangle;
    using Viewport = DirectX::SimpleMath::Viewport;
    using Ray = DirectX::SimpleMath::Ray;
    using Plane = DirectX::SimpleMath::Plane;

    // Collision types
    using BoundingSphere = DirectX::BoundingSphere;
    using BoundingBox = DirectX::BoundingBox;
    using BoundingOrientedBox = DirectX::BoundingOrientedBox;
    using BoundingFrustum = DirectX::BoundingFrustum;

} // Neb namespace