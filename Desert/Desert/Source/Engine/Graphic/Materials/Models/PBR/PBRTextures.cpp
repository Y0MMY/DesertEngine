#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRConstants.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::Models::PBR
{
    PBRMaterialTexture::PBRMaterialTexture(
         const std::shared_ptr<Material>&                   baseMaterial,
         const std::shared_ptr<Uniforms::UniformImageCube>& uniformIrradianceMap,
         const std::shared_ptr<Uniforms::UniformImageCube>& uniformPreFilteredMap )
         : MaterialWrapperTextureCubeArray( baseMaterial, { uniformIrradianceMap, uniformPreFilteredMap } ),
           m_UniformIrradianceMap( uniformIrradianceMap ), m_UniformPreFilteredMap( uniformPreFilteredMap ),
           m_PBRPBRTextures( {} )
    {
    }

    void PBRMaterialTexture::UpdatePBR( PBRTextures&& pbr )
    {
        m_PBRPBRTextures = std::move( pbr );
        m_UniformPreFilteredMap->SetImageCube( m_PBRPBRTextures.PreFilteredMap );
        m_UniformIrradianceMap->SetImageCube( m_PBRPBRTextures.IrradianceMap );
        // m_Material->SetImage2D( std::string( Constatns::SpecularBRDF_LUT ),
        //       Renderer::GetInstance().GetBRDFTexture()->GetImage2D() );
    }

    const std::string_view PBRMaterialTexture::GetUniformIrradianceName()
    {
        return Constatns::IrradianceTexture;
    }

    const std::string_view PBRMaterialTexture::GetUniformPreFilteredName()
    {
        return Constatns::SpecularTexture;
    }

} // namespace Desert::Graphic::Models::PBR