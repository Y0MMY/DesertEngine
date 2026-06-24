#include "JumpFloodMaterials.hpp"

namespace Desert::Graphic
{
    MaterialJFAInit::MaterialJFAInit() : Material( "MaterialJFAInit", "JFA_Init" )
    {
        m_MaskTexture = m_MaterialExecutor->GetTexture2DProperty( "u_StencilTexture" ).get();
    }

    void MaterialJFAInit::Bind( const Image2D* maskImage )
    {
        if ( m_MaskTexture && maskImage )
        {
            m_MaskTexture->SetImage( maskImage );
        }
    }

    MaterialJFAStep::MaterialJFAStep() : Material( "MaterialJFAStep", "JFA_Step" )
    {
        m_InputTexture = m_MaterialExecutor->GetTexture2DProperty( "u_InputTexture" ).get();
    }

    void MaterialJFAStep::Bind( const Image2D* inputSeed, int stepLength )
    {
        if ( m_InputTexture && inputSeed )
        {
            m_InputTexture->SetImage( inputSeed );
        }
        m_MaterialExecutor->PushConstant( &stepLength, sizeof( int ) );
    }
} // namespace Desert::Graphic
