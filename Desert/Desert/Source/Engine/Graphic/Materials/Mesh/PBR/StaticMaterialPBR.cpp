#include "StaticMaterialPBR.hpp"
#include "MaterialPBRBase.hpp"
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    StaticMaterialPBR::StaticMaterialPBR() : Material( "PBRMaterial", "StaticMeshPBR" )
    {
        // MPROPERTY members auto-register their defaults via the Registrar pattern.
    }

    void StaticMaterialPBR::SetAlbedo( MaterialInstance* instance, const Image2D* texture, const glm::vec3& color )
    {
        instance->SetVec3( "AlbedoColor", color );
        if ( texture )
            instance->SetTexture( "u_AlbedoTexture", const_cast<Image2D*>( texture ) );
    }

    void StaticMaterialPBR::SetNormalMap( MaterialInstance* instance, const Image2D* texture )
    {
        if ( texture )
            instance->SetTexture( "u_NormalTexture", const_cast<Image2D*>( texture ) );
    }

    void StaticMaterialPBR::SetMetallic( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "MetallicValue", value );
        (void)texture;
    }

    void StaticMaterialPBR::SetRoughness( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "RoughnessValue", value );
        (void)texture;
    }

    void StaticMaterialPBR::SetAmbientOcclusion( MaterialInstance* instance, const Image2D* texture )
    {
        (void)instance;
        (void)texture;
    }

    void StaticMaterialPBR::SetEmissive( MaterialInstance* instance, const Image2D* texture, float intensity )
    {
        instance->SetFloat( "EmissionStrength", intensity );
        (void)texture;
    }

    void StaticMaterialPBR::UpdateTransform( MaterialInstance* instance, const glm::mat4& transform )
    {
        instance->SetMat4( "Transform", transform );
    }

    void StaticMaterialPBR::UpdateCamera( MaterialInstance* instance, const Core::Camera* camera )
    {
        MaterialPBRBase::UpdateCamera( instance, camera );
    }

    void StaticMaterialPBR::UpdateLights( MaterialInstance* instance, const ShaderProtocols::PointLight& pointLights,
                                          const ShaderProtocols::DirectionLight& dirLights )
    {
        MaterialPBRBase::UpdateLights( instance, pointLights, dirLights );
    }

    void StaticMaterialPBR::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        // Push constant (Transform is not a UB field — handled separately)
        glm::mat4 transform = instance->GetMat4( "Transform" );
        m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ) );

        // Base handles: TProperty defaults → FieldProperty, instance overrides, UB flush
        Material::Bind( instance );
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
        // All property upload is handled by Material::Bind
    }
} // namespace Desert::Graphic
