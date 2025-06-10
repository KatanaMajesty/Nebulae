#pragma once

#include <numbers>
#include <concepts>
#include <limits>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers.
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif // Exclude rarely-used stuff from Windows headers.

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
    using Quaternion = DirectX::SimpleMath::Quaternion;

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

    template<typename T>
    inline T ToRadians(T t)
    {
        static constexpr float Mul = std::numbers::pi_v<float> / 180.0f;
        return t * Mul;
    }

    static constexpr float Inf = std::numeric_limits<float>::infinity();

    struct AABB
    {
        Vec3 min = Vec3(Inf);
        Vec3 max = Vec3(-Inf);
    };

#if 0
    template<typename T> 
    int32_t Signum(T val) { return (T(0) < val) - (val < T(0)); }
#else
    // 16.06.24 Use bitwise integer sign calculation instead
    template<std::integral T>
    int32_t Signum(T val) { return (T(1) | (val >> sizeof(T) * CHAR_BIT - T(1))); }
#endif

} // Neb namespace