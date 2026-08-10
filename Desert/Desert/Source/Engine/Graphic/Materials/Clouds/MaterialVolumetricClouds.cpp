#include "MaterialVolumetricClouds.hpp"

namespace Desert::Graphic
{
    MaterialVolumetricClouds::MaterialVolumetricClouds() : Material( "MaterialVolumetricClouds", "CloudComposite" )
    {
        m_ScatterTexture = m_MaterialExecutor->GetTexture2DProperty( "u_CloudScatter" ).get();
    }

    void MaterialVolumetricClouds::Bind( const Image2D* scatterImage )
    {
        if ( m_ScatterTexture && scatterImage )
            m_ScatterTexture->SetImage( scatterImage );
    }
} // namespace Desert::Graphic
