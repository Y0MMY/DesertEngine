#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRConstants.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::Models::PBR
{

    void PBRMaterialTexture::UpdatePBR( PBRTextures&& pbr )
    {
        m_PBRPBRTextures = std::move( pbr );
        m_Material->SetImageCube( std::string( Constatns::IrradianceTexture ), m_PBRPBRTextures.IrradianceMap );
        m_Material->SetImageCube( std::string( Constatns::SpecularTexture ), m_PBRPBRTextures.PreFilteredMap );
     //   m_Material->SetImage2D( std::string( Constatns::SpecularBRDF_LUT ),
                             //   Renderer::GetInstance().GetBRDFTexture()->GetImage2D() );
    }

} // namespace Desert::Graphic::Models::PBR