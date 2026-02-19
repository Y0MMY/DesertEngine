#include "MaterialTonemap.hpp"

namespace Desert::Graphic
{
    MaterialTonemap::MaterialTonemap() : Material( "MaterialTonemap", "SceneComposite" )
    {
        m_TonemapBinding = std::make_unique<MaterialHelper::TonemapBinding>( m_MaterialExecutor.get() );
    }

    void MaterialTonemap::Bind( const std::shared_ptr<Image2D>& targetImage )
    {
        m_TonemapBinding->UpdateTexture( targetImage.get() );
    }
} // namespace Desert::Graphic