#include "MaterialAutoExposure.hpp"

namespace Desert::Graphic
{
    MaterialAutoExposure::MaterialAutoExposure() : Material( "MaterialAutoExposure", "AutoExposure" )
    {
        m_SceneTexture  = m_MaterialExecutor->GetTexture2DProperty( "u_SceneTexture" ).get();
        m_PrevLuminance = m_MaterialExecutor->GetTexture2DProperty( "u_PrevLuminance" ).get();
    }

    void MaterialAutoExposure::Bind( const Image2D* scene, const Image2D* prevLuminance, const Params& params )
    {
        if ( m_SceneTexture && scene )
            m_SceneTexture->SetImage( scene );
        if ( m_PrevLuminance && prevLuminance )
            m_PrevLuminance->SetImage( prevLuminance );
        m_MaterialExecutor->PushConstant( &params, sizeof( Params ) );
    }
} // namespace Desert::Graphic
