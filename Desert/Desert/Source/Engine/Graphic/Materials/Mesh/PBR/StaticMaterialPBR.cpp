#include "StaticMaterialPBR.hpp"
#include "MaterialPBRBase.hpp"
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    namespace
    {
        // Layout must match the push-constant block in Static.glsl.vert / PBR.glsl.frag (offset 64,
        // right after the mat4 Transform). Blend factors are folded in here so the shader gets final
        // values.
        struct PBRPushMaterial
        {
            glm::vec4 AlbedoAO;           // rgb = albedo * blend, a = ambient occlusion
            glm::vec4 MetalRoughEmission; // x = metallic, y = roughness, z = emission strength
            glm::vec4 EmissionColor;      // rgb = emission color
        };

        // Effective value = per-instance override if present, otherwise the material-level default.
        PBRPushMaterial BuildPushMaterial( StaticMaterialPBR* material, const MaterialInstance* instance )
        {
            const auto f = [&]( const std::string& name, float def )
            { return instance->HasParameter( name ) ? instance->GetFloat( name, def ) : def; };
            const auto v3 = [&]( const std::string& name, const glm::vec3& def )
            { return instance->HasParameter( name ) ? instance->GetVec3( name, def ) : def; };

            const glm::vec3 albedo    = v3( "AlbedoColor", material->GetAlbedoColor() ) *
                                        f( "AlbedoBlend", material->GetAlbedoBlend() );
            const float metallic      = f( "MetallicValue", material->GetMetallicValue() ) *
                                        f( "MetallicBlend", material->GetMetallicBlend() );
            const float roughness     = f( "RoughnessValue", material->GetRoughnessValue() ) *
                                        f( "RoughnessBlend", material->GetRoughnessBlend() );
            const glm::vec3 emission  = v3( "EmissionColor", material->GetEmissionColor() );
            const float emissionPower = f( "EmissionStrength", material->GetEmissionStrength() );
            const float ao            = f( "AOValue", material->GetAOValue() );

            return { glm::vec4( albedo, ao ), glm::vec4( metallic, roughness, emissionPower, 0.0f ),
                     glm::vec4( emission, 0.0f ) };
        }
    } // namespace

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

        // Push constant (Transform is not a UB field — handled separately). RenderMesh overwrites the
        // transform sub-block per submesh; this seeds offset 0 for the common single-submesh case.
        glm::mat4 transform = instance->GetMat4( "Transform" );
        m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ), 0 );

        // Per-object PBR material params go into the push-constant range right after the transform, so
        // every draw carries its own values (a shared uniform buffer would collapse to the last object).
        const PBRPushMaterial pushMaterial = BuildPushMaterial( this, instance );
        m_MaterialExecutor->PushConstant( &pushMaterial, sizeof( pushMaterial ), sizeof( glm::mat4 ) );

        // Base handles: TProperty defaults → FieldProperty, instance overrides, UB flush (textures,
        // camera and lights are still uniform buffers shared across objects of this material).
        Material::Bind( instance );
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
        // All property upload is handled by Material::Bind
    }
} // namespace Desert::Graphic
