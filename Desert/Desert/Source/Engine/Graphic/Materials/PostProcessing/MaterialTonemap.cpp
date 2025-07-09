#include "MaterialTonemap.hpp"

namespace Desert::Graphic
{
    MaterialTonemap::MaterialTonemap()
    {
        const auto& shader = Graphic::ShaderLibrary::Get( "SceneComposite.glsl", {} );
        m_Material         = Graphic::MaterialExecutor::Create( "MaterialTonemap", shader.GetValue() );
        m_ToneMapModel     = std::make_unique<Models::ToneMap>( m_Material );
    }

    void MaterialTonemap::UpdateRenderParameters( const std::shared_ptr<Image2D>& targetImage )
    {
        m_ToneMapModel->UpdateToneMap( targetImage );
    }
} // namespace Desert::Graphic