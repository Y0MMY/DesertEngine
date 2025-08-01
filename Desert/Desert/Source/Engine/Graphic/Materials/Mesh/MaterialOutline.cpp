#include "MaterialOutline.hpp"

namespace Desert::Graphic
{
    MaterialOutline::MaterialOutline()
    {
        const auto& shader = Graphic::ShaderLibrary::Get( "Outline.glsl", {} );

        m_Material = Graphic::MaterialExecutor::Create( "MaterialOutline", shader.GetValue() );

        m_OutlineData = std::make_unique<Models::OutlineData>( m_Material );
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

        m_Material->PushConstant( &vp, sizeof( vp ) );

        m_OutlineData->UpdateOutlineUB( { width, color } );
    }

} // namespace Desert::Graphic