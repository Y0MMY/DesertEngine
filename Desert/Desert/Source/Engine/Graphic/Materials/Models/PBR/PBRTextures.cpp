#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRConstants.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::Models::PBR
{
    PBRMaterialTexture::PBRMaterialTexture( const std::shared_ptr<MaterialExecutor>& baseMaterial )
         : MaterialWrapperTextureCubeArray( baseMaterial, { std::string( Constatns::IrradianceTexture ),
                                                            std::string( Constatns::SpecularTexture ) } ),
           MaterialWrapperTexture2D( baseMaterial, std::string( Constatns::SpecularBRDF_LUT ) ),
           m_PBRPBRTextures( {} )
    {
    }

    void PBRMaterialTexture::UpdatePBR( const std::optional<PBRTextures>& pbr )
    {
        if ( pbr )
        {
            m_PBRPBRTextures = *pbr;
        }

        m_UniformProperties[1]->SetTexture( m_PBRPBRTextures.PreFilteredMap );
        m_UniformProperties[0]->SetTexture( m_PBRPBRTextures.IrradianceMap );
        m_UniformProperty->SetImage( Renderer::GetInstance().GetBRDFTexture()->GetImage2D() );
    }

    const std::string_view PBRMaterialTexture::GetUniformIrradianceName()
    {
        return Constatns::IrradianceTexture;
    }

    const std::string_view PBRMaterialTexture::GetUniformPreFilteredName()
    {
        return Constatns::SpecularTexture;
    }

    const std::string_view PBRMaterialTexture::GetUniformBRDFLutName()
    {
        return Constatns::SpecularBRDF_LUT;
    }
} // namespace Desert::Graphic::Models::PBR