#include "MaterialJFAComposite.hpp"

namespace Desert::Graphic
{
    MaterialJFAComposite::MaterialJFAComposite() : Material( "MaterialJFAComposite", "JFA_Final" )
    {
        m_JFATexture   = m_MaterialExecutor->GetTexture2DProperty( "u_JFATexture" ).get();
        m_SceneTexture = m_MaterialExecutor->GetTexture2DProperty( "u_SceneTexture" ).get();
    }

    void MaterialJFAComposite::Bind( const Image2D* jfaSeed, const Image2D* sceneColor,
                                     const glm::vec4& outlineColor, float outlineWidth, float smoothness )
    {
        if ( m_JFATexture && jfaSeed )
            m_JFATexture->SetImage( jfaSeed );

        if ( m_SceneTexture && sceneColor )
            m_SceneTexture->SetImage( sceneColor );

        // UBO field reflection is not populated at runtime (VulkanShader::Reflect fills only block
        // name/size, not individual members), so UploadRegisteredProperties / FindFieldInAnyUB
        // would always return null and leave u_OutlineWidth = 0 in the shader, triggering the
        // early-out that suppresses the outline. Write the UBO as a raw blob instead.
        struct JFAFinalUB
        {
            glm::vec4 OutlineColor;
            float     OutlineWidth;
            float     Smoothness;
        };
        const JFAFinalUB ub{ .OutlineColor = outlineColor,
                             .OutlineWidth  = outlineWidth,
                             .Smoothness    = smoothness };

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "JFAFinalUB" ) )
            prop->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );
    }
} // namespace Desert::Graphic
