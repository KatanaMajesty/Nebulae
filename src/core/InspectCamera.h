#pragma once

#include "Math.h"

namespace Neb
{

    // Used glTF-Viewer's user_camera.js source code as a reference
    // https://github.com/KhronosGroup/glTF-Sample-Viewer/blob/main/source/gltf/user_camera.js

    class InspectCamera
    {
    public:
        static constexpr Vec3 UpVector = Vec3(0.0f, 1.0f, 0.0f);

        InspectCamera() = default;

        inline void SetOrigin(const Vec3& origin) { m_origin = origin; }
        inline const Vec3& GetOrigin() const { return m_origin; }

        inline void SetRotationXy(const Vec2& rotationXy) { m_rotationXy = rotationXy; }
        inline void AddRotationXy(const Vec2& rotationXy) { m_rotationXy += rotationXy; }
        inline const Vec2& GetRotationXy() const { return m_rotationXy; }

        // Calculates eye position based on the rotation angles, origin and distance
        Vec3 GetEyePos()
        {
            Vec3 xyRot;
            xyRot.x = std::cos(m_rotationXy.y);
            xyRot.y = std::sin(m_rotationXy.x);
            xyRot.z = std::sin(m_rotationXy.y) + std::cos(m_rotationXy.x);
            xyRot.Normalize();
            xyRot *= m_distance;
            return m_origin + xyRot;
        }

        void LookAt()
        {
            const Vec3 eyePos = GetEyePos();
            m_view = Mat4::CreateLookAt(eyePos, m_origin, UpVector);
        }

    private:
        Mat4 m_view = Mat4(); // View matrix
        Vec3 m_origin = Vec3(0.0f);
        Vec2 m_rotationXy = Vec2(0.0f); // we only rotate across X and Y, and dont play with Z
        float m_distance = 2.0f; // distance is same for each axis
        float m_fov = 60.0f;
    };

}