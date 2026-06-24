#include "MaterialJFAComposite.hpp"

#include <unordered_set>

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

        SetOutlineColor( outlineColor );
        SetOutlineWidth( outlineWidth );
        SetSmoothness( smoothness );

        std::unordered_set<UniformBufferProperty*> dirtyUBs;
        UploadRegisteredProperties( dirtyUBs );
        for ( auto* ub : dirtyUBs )
            ub->UpdateFields();
    }
} // namespace Desert::Graphic
