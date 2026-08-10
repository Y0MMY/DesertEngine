#include "MaterialVolumetricClouds.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Graphic
{
    MaterialVolumetricClouds::MaterialVolumetricClouds() : Material( "MaterialVolumetricClouds", "CloudComposite" )
    {
        m_ResolvedTexture   = m_MaterialExecutor->GetTexture2DProperty( "u_CloudResolved" ).get();
        m_DepthGuideTexture = m_MaterialExecutor->GetTexture2DProperty( "u_CloudDepthGuide" ).get();

        // Reflection did not hand us a sampler the composite declares. Binding would then be a no-op and
        // the pass would quietly composite the fallback texture over the whole screen — a full-screen
        // artefact with no error anywhere. Say which name was looked for.
        if ( !m_ResolvedTexture )
            LOG_ERROR( "[Clouds] The CloudComposite shader has no sampler named 'u_CloudResolved'; the "
                       "cloud composite would draw the fallback texture." );
        if ( !m_DepthGuideTexture )
            LOG_ERROR( "[Clouds] The CloudComposite shader has no sampler named 'u_CloudDepthGuide'; the "
                       "bilateral upsample would weight every tap by a fallback distance." );
    }

    void MaterialVolumetricClouds::Bind( const Image2D* resolvedImage, const Image2D* depthGuide )
    {
        if ( m_ResolvedTexture && resolvedImage )
            m_ResolvedTexture->SetImage( resolvedImage );
        if ( m_DepthGuideTexture && depthGuide )
            m_DepthGuideTexture->SetImage( depthGuide );
    }
} // namespace Desert::Graphic
