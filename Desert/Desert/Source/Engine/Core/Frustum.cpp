#include "Frustum.hpp"

namespace Desert::Core
{

    Frustum::Frustum( const glm::mat4& projection, const glm::mat4& view )
    {
        Rebuild( projection, view );
    }

    bool Frustum::IsInside( const glm::vec3& point ) const
    {
        for ( const auto& plane : m_Planes )
        {
            if ( plane.GetDistance( point ) < 0.0f )
            {
                return false;
            }
        }
        return true;
    }

    void Frustum::Rebuild( const glm::mat4& projection, const glm::mat4& view )
    {
        glm::mat4 viewProjection = projection * view;

        // Left plane
        m_Planes[PLANE_LEFT].Normal.x = viewProjection[0][3] + viewProjection[0][0];
        m_Planes[PLANE_LEFT].Normal.y = viewProjection[1][3] + viewProjection[1][0];
        m_Planes[PLANE_LEFT].Normal.z = viewProjection[2][3] + viewProjection[2][0];
        m_Planes[PLANE_LEFT].Distance = viewProjection[3][3] + viewProjection[3][0];

        // Right plane
        m_Planes[PLANE_RIGHT].Normal.x = viewProjection[0][3] - viewProjection[0][0];
        m_Planes[PLANE_RIGHT].Normal.y = viewProjection[1][3] - viewProjection[1][0];
        m_Planes[PLANE_RIGHT].Normal.z = viewProjection[2][3] - viewProjection[2][0];
        m_Planes[PLANE_RIGHT].Distance = viewProjection[3][3] - viewProjection[3][0];

        // Bottom plane
        m_Planes[PLANE_DOWN].Normal.x = viewProjection[0][3] + viewProjection[0][1];
        m_Planes[PLANE_DOWN].Normal.y = viewProjection[1][3] + viewProjection[1][1];
        m_Planes[PLANE_DOWN].Normal.z = viewProjection[2][3] + viewProjection[2][1];
        m_Planes[PLANE_DOWN].Distance = viewProjection[3][3] + viewProjection[3][1];

        // Top plane
        m_Planes[PLANE_UP].Normal.x = viewProjection[0][3] - viewProjection[0][1];
        m_Planes[PLANE_UP].Normal.y = viewProjection[1][3] - viewProjection[1][1];
        m_Planes[PLANE_UP].Normal.z = viewProjection[2][3] - viewProjection[2][1];
        m_Planes[PLANE_UP].Distance = viewProjection[3][3] - viewProjection[3][1];

        // Near plane
        m_Planes[PLANE_NEAR].Normal.x = viewProjection[0][3] + viewProjection[0][2];
        m_Planes[PLANE_NEAR].Normal.y = viewProjection[1][3] + viewProjection[1][2];
        m_Planes[PLANE_NEAR].Normal.z = viewProjection[2][3] + viewProjection[2][2];
        m_Planes[PLANE_NEAR].Distance = viewProjection[3][3] + viewProjection[3][2];

        // Far plane
        m_Planes[PLANE_FAR].Normal.x = viewProjection[0][3] - viewProjection[0][2];
        m_Planes[PLANE_FAR].Normal.y = viewProjection[1][3] - viewProjection[1][2];
        m_Planes[PLANE_FAR].Normal.z = viewProjection[2][3] - viewProjection[2][2];
        m_Planes[PLANE_FAR].Distance = viewProjection[3][3] - viewProjection[3][2];

        // Normalize all planes
        for ( auto& plane : m_Planes )
        {
            float length = glm::length( plane.Normal );
            plane.Normal /= length;
            plane.Distance /= length;
        }
    }

} // namespace Desert::Core