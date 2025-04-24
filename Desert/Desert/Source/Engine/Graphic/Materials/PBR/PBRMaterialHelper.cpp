#include <Engine/Graphic/Materials/PBR/PBRMaterialHelper.hpp>

namespace Desert::Graphic::MaterialHelpers
{
    PBRMaterial::PBRMaterial( const std::shared_ptr<Material>& baseMaterial ) : m_Material( baseMaterial )
    {
    }

    void PBRMaterial::SetAlbedo( const glm::vec3& color )
    {
        m_CurrentParams.Albedo = color;
    }

    void PBRMaterial::SetMetallic( float value )
    {
        m_CurrentParams.Metallic = value;
    }

    void PBRMaterial::SetRoughness( float value )
    {
        m_CurrentParams.Roughness = value;
    }

    void PBRMaterial::ApplyAll( const PBRUniforms& params )
    {
        m_CurrentParams = params;
    }

    void PBRMaterial::Override( const std::shared_ptr<UniformBuffer>& uniform ) const
    {
        uniform->SetData( (void*)&m_CurrentParams, sizeof( PBRUniforms ), 0 );
        m_Material->AddUniformToOverride( uniform );
    }

} // namespace Desert::Graphic::MaterialHelpers