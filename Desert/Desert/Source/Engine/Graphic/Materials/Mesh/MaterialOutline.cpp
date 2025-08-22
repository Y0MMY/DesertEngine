#include "MaterialOutline.hpp"

namespace Desert::Graphic
{
    MaterialOutline::MaterialOutline() : Material( "MaterialOutline", "Outline.glsl" )
    {
        m_OutlineData = std::make_unique<Models::OutlineData>( m_MaterialExecutor );
    }

    static struct VP
    {
        glm::mat4 ViewProjection;
        glm::mat4 Transform;
    };

    void MaterialOutline::UpdateRenderParameters( const Core::Camera& camera, const glm::mat4& transform,
                                                  const float width, const glm::vec3& color )
    {
        const VP vp{ .ViewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix(),
                     .Transform      = transform };

        const glm::vec3 worldPosition  = transform[3];
        const auto&     cameraPosition = camera.GetPosition();

        float       distance     = glm::distance( worldPosition, cameraPosition );
        const float dynamicWidth = width / ( 1.0f + distance );

        m_MaterialExecutor->PushConstant( &vp, sizeof( vp ) );

        m_OutlineData->UpdateOutlineUB( { 1.0f + dynamicWidth, color } );
    }

} // namespace Desert::Graphic