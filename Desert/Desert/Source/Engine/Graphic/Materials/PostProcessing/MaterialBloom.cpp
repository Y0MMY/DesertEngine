#include "MaterialBloom.hpp"

namespace Desert::Graphic
{
    MaterialBloomBright::MaterialBloomBright() : Material( "MaterialBloomBright", "BloomBright" )
    {
        m_InputTexture = m_MaterialExecutor->GetTexture2DProperty( "u_InputTexture" ).get();
    }

    void MaterialBloomBright::Bind( const Image2D* input, float threshold )
    {
        if ( m_InputTexture && input )
            m_InputTexture->SetImage( input );
        m_MaterialExecutor->PushConstant( &threshold, sizeof( float ) );
    }

    MaterialBloomBlur::MaterialBloomBlur() : Material( "MaterialBloomBlur", "BloomBlur" )
    {
        m_InputTexture = m_MaterialExecutor->GetTexture2DProperty( "u_InputTexture" ).get();
    }

    void MaterialBloomBlur::Bind( const Image2D* input, const glm::vec2& direction )
    {
        if ( m_InputTexture && input )
            m_InputTexture->SetImage( input );
        m_MaterialExecutor->PushConstant( &direction, sizeof( glm::vec2 ) );
    }
} // namespace Desert::Graphic
