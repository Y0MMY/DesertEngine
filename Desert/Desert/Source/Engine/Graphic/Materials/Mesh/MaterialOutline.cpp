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

    void MaterialOutline::Bind( const UpdateMaterialOutlineInfo& data )
    {
        const VP vp{ .ViewProjection = data.Camera->GetProjectionMatrix() * data.Camera->GetViewMatrix(),
                     .Transform      = data.Transform };

        const glm::vec3 worldPosition  = data.Transform[3];
        const auto&     cameraPosition = data.Camera->GetPosition();

        float       distance     = glm::distance( worldPosition, cameraPosition );
        const float dynamicWidth = data.Width / ( 1.0f + distance );

        m_MaterialExecutor->PushConstant( &vp, sizeof( vp ) );

        m_OutlineData->UpdateOutlineUB( { 1.0f + dynamicWidth, data.Color } );
    }

} // namespace Desert::Graphic