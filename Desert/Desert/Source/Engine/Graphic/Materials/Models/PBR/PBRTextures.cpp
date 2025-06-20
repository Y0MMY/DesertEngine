#include <Engine/Graphic/Materials/Models/PBR/PBRTextures.hpp>
#include <Engine/Graphic/Materials/Models/PBR/PBRConstants.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic::Models::PBR
{
    PBRMaterialTexture::PBRMaterialTexture(
         const std::shared_ptr<Material>&                   baseMaterial,
         const std::shared_ptr<Uniforms::UniformImageCube>& uniformIrradianceMap,
         const std::shared_ptr<Uniforms::UniformImageCube>& uniformPreFilteredMap,
         const std::shared_ptr<Uniforms::UniformImage2D>&   uniformBRDF )
         : MaterialWrapperTextureCubeArray( baseMaterial, { uniformIrradianceMap, uniformPreFilteredMap } ),
           MaterialWrapperTexture2D( baseMaterial, uniformBRDF ), m_UniformIrradianceMap( uniformIrradianceMap ),
           m_UniformPreFilteredMap( uniformPreFilteredMap ), m_UniformBRDF( uniformBRDF ), m_PBRPBRTextures( {} )
    {
    }

    void PBRMaterialTexture::UpdatePBR( PBRTextures&& pbr )
    {
        m_PBRPBRTextures = std::move( pbr );
        m_UniformPreFilteredMap->SetImageCube( m_PBRPBRTextures.PreFilteredMap );
        m_UniformIrradianceMap->SetImageCube( m_PBRPBRTextures.IrradianceMap );
        m_UniformBRDF->SetImage2D( Renderer::GetInstance().GetBRDFTexture()->GetImage2D() );
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

    void PBRMaterialTexture::Bind()
    {
        MaterialWrapperTextureCubeArray::Bind();
        MaterialWrapperTexture2D::Bind();
    }
} // namespace Desert::Graphic::Models::PBR