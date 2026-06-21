#include "MaterialTonemap.hpp"

namespace Desert::Graphic
{
    MaterialTonemap::MaterialTonemap() : Material( "MaterialTonemap", "SceneComposite" )
    {
        m_GeometryTexture = m_MaterialExecutor->GetTexture2DProperty( "u_GeometryTexture" ).get();
    }

    void MaterialTonemap::Bind( const std::shared_ptr<Image2D>& targetImage )
    {
        if ( m_GeometryTexture && targetImage )
            m_GeometryTexture->SetImage( targetImage.get() );
    }
} // namespace Desert::Graphic
