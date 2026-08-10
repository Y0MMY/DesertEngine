#include "MaterialVolumetricClouds.hpp"

#include <Common/Core/Logger.hpp>

namespace Desert::Graphic
{
    MaterialVolumetricClouds::MaterialVolumetricClouds() : Material( "MaterialVolumetricClouds", "CloudComposite" )
    {
        m_ScatterTexture = m_MaterialExecutor->GetTexture2DProperty( "u_CloudScatter" ).get();
        if ( !m_ScatterTexture )
        {
            // Reflection did not hand us the sampler the composite declares. Binding would then be a
            // no-op and the pass would quietly composite the fallback texture over the whole screen —
            // a full-screen artefact with no error anywhere. Say which name was looked for.
            LOG_ERROR( "[Clouds] The CloudComposite shader has no sampler named 'u_CloudScatter'; the "
                       "cloud composite would draw the fallback texture." );
        }
    }

    void MaterialVolumetricClouds::Bind( const Image2D* scatterImage )
    {
        if ( m_ScatterTexture && scatterImage )
            m_ScatterTexture->SetImage( scatterImage );
    }
} // namespace Desert::Graphic
