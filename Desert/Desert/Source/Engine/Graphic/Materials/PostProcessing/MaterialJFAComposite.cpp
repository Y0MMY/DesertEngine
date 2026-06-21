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

        // Update MPROPERTY values — Material::Bind is NOT called here (no MaterialInstance),
        // so push to UB fields directly via the cached executor.
        SetOutlineColor( outlineColor );
        SetOutlineWidth( outlineWidth );
        SetSmoothness( smoothness );

        // Flush dirty TProperty fields to the UB
        std::unordered_set<UniformBufferProperty*> dirtyUBs;
        UploadRegisteredProperties( dirtyUBs );
        for ( auto* ub : dirtyUBs )
            ub->UpdateFields();
    }
} // namespace Desert::Graphic
