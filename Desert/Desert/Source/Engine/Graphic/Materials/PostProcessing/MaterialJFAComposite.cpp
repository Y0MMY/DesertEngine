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

        // Flush EVERY UB that still has dirty fields — NOT just the ones touched this frame. A field
        // stays dirty for frames-in-flight frames, so this writes the value into EACH per-frame-in-flight
        // buffer copy. TProperty::Set skips re-marking when the value is unchanged (the outline color is
        // constant every frame), so relying on `dirtyUBs` alone updates only the first frame's copy and
        // leaves the others uninitialized — that was the yellow<->white outline flicker.
        for ( const auto& [ubName, idx] : m_MaterialExecutor->GetUniformBufferProperties() )
        {
            auto ubProp = m_MaterialExecutor->GetUniformBufferProperty( ubName );
            if ( ubProp && ubProp->HasDirtyFields() )
                ubProp->UpdateFields();
        }
    }
} // namespace Desert::Graphic
