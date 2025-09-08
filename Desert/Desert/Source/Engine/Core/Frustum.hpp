#pragma once

#include <glm/glm.hpp>

namespace Desert::Core
{
    class Camera;

    enum FrustumPlane
    {
        PLANE_NEAR = 0,
        PLANE_LEFT,
        PLANE_RIGHT,
        PLANE_UP,
        PLANE_DOWN,
        PLANE_FAR,
    };

    class Frustum final
    {
    public:
        struct Plane
        {
            glm::vec3 Normal;
            float     Distance;

            float GetDistance( const glm::vec3& point ) const
            {
                return glm::dot( point, Normal ) + Distance;
            }
        };

    public:
        Frustum() = default;

        explicit Frustum( const glm::mat4& projection, const glm::mat4& view );

        void Rebuild( const glm::mat4& projection, const glm::mat4& view );

        bool IsInside( const glm::vec3& point ) const;

    private:
        std::array<Plane, 6> m_Planes;
    };
} // namespace Desert::Core