#include "MaterialJFAComposite.hpp"

namespace Desert::Graphic
{
    MaterialJFAComposite::MaterialJFAComposite() : Material( "MaterialJFAComposite", "JFA_Final" )
    {
        m_JFATexture   = m_MaterialExecutor->GetTexture2DProperty( "u_JFATexture" ).get();
        m_SceneTexture = m_MaterialExecutor->GetTexture2DProperty( "u_SceneTexture" ).get();
    }

    void MaterialJFAComposite::Bind( const Image2D* jfaSeed, const Image2D* sceneColor,
                                     const JFACompositeParams& params )
    {
        if ( m_JFATexture && jfaSeed )
        {
            m_JFATexture->SetImage( jfaSeed );
        }
        if ( m_SceneTexture && sceneColor )
        {
            m_SceneTexture->SetImage( sceneColor );
        }

        const JFAFinalUB ub{ .OutlineColor = glm::vec4( params.OutlineColor, 1.0f ),
                             .OutlineWidth = params.OutlineWidth,
                             .Smoothness   = params.Smoothness };

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "JFAFinalUB" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &ub ), sizeof( ub ) );
        }
    }
} // namespace Desert::Graphic
