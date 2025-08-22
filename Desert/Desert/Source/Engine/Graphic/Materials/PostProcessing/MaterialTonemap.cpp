#include "MaterialTonemap.hpp"

namespace Desert::Graphic
{
    MaterialTonemap::MaterialTonemap() : Material( "MaterialTonemap", "SceneComposite.glsl" )
    {
        m_ToneMapModel = std::make_unique<Models::ToneMap>( m_MaterialExecutor );
    }

    void MaterialTonemap::UpdateRenderParameters( const std::shared_ptr<Image2D>& targetImage )
    {
        m_ToneMapModel->UpdateToneMap( targetImage );
    }
} // namespace Desert::Graphic