#include "StaticMaterialPBR.hpp"
#include "MaterialPBRBase.hpp"
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    StaticMaterialPBR::StaticMaterialPBR() : Material( "PBRMaterial", "StaticMeshPBR" )
    {
        // Set default parameters matching shader property names in PBR.glsl.frag
        SetDefaultParameter( "AlbedoColor", glm::vec3( 1.0f ), MaterialPropertyType::Vec3 );
        SetDefaultParameter( "AlbedoBlend", 1.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "MetallicValue", 0.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "MetallicBlend", 1.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "RoughnessValue", 0.5f, MaterialPropertyType::Float );
        SetDefaultParameter( "RoughnessBlend", 1.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "EmissionColor", glm::vec3( 0.0f ), MaterialPropertyType::Vec3 );
        SetDefaultParameter( "EmissionStrength", 1.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "AOValue", 1.0f, MaterialPropertyType::Float );
    }

    void StaticMaterialPBR::SetAlbedo( MaterialInstance* instance, const Image2D* texture, const glm::vec3& color )
    {
        instance->SetVec3( "AlbedoColor", color );
        if ( texture )
        {
            instance->SetTexture( "u_AlbedoTexture", const_cast<Image2D*>( texture ) );
        }
    }

    void StaticMaterialPBR::SetNormalMap( MaterialInstance* instance, const Image2D* texture )
    {
        if ( texture )
        {
            instance->SetTexture( "u_NormalTexture", const_cast<Image2D*>( texture ) );
        }
    }

    void StaticMaterialPBR::SetMetallic( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "MetallicValue", value );
        if ( texture )
        {
            // Update MetallicTexture if needed
        }
    }

    void StaticMaterialPBR::SetRoughness( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "RoughnessValue", value );
        if ( texture )
        {
            // Update RoughnessTexture if needed
        }
    }

    void StaticMaterialPBR::SetAmbientOcclusion( MaterialInstance* instance, const Image2D* texture )
    {
        if ( texture )
        {
            // Update AOTexture if needed
        }
    }

    void StaticMaterialPBR::SetEmissive( MaterialInstance* instance, const Image2D* texture, float intensity )
    {
        instance->SetFloat( "EmissionStrength", intensity );
        if ( texture )
        {
            // Update EmissiveTexture if needed
        }
    }

    void StaticMaterialPBR::UpdateTransform( MaterialInstance* instance, const glm::mat4& transform )
    {
        // For static meshes, transform is usually in push constants, but we store it in instance for generality
        instance->SetMat4( "ModelMatrix", transform );
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

        // Update push constants for transform (direct from instance)
        glm::mat4 transform = instance->GetMat4( "ModelMatrix" );
        m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ) );

        // Base Material::Bind will apply all MaterialInstance property overrides to the executor
        Material::Bind( instance );
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
        // Reconstruct uniform struct from instance properties for single-call GPU update
        PBRUniforms uniforms;
        uniforms.AlbedoColor     = instance->GetVec3( "AlbedoColor" );
        uniforms.AlbedoBlend     = instance->GetFloat( "AlbedoBlend" );
        uniforms.MetallicValue   = instance->GetFloat( "MetallicValue" );
        uniforms.MetallicBlend   = instance->GetFloat( "MetallicBlend" );
        uniforms.RoughnessValue  = instance->GetFloat( "RoughnessValue" );
        uniforms.RoughnessBlend  = instance->GetFloat( "RoughnessBlend" );
        uniforms.EmissionColor   = instance->GetVec3( "EmissionColor" );
        uniforms.EmissionStrength = instance->GetFloat( "EmissionStrength" );
        uniforms.AOValue         = instance->GetFloat( "AOValue" );

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "PBRMaterialPropertiesUB" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &uniforms ), sizeof( PBRUniforms ) );
        }

        // Texture bindings from instance
        if ( auto tex = static_cast<Image2D*>( instance->GetTexture( "u_AlbedoTexture" ) ) )
        {
            if ( auto prop = m_MaterialExecutor->GetTexture2DProperty( "u_AlbedoTexture" ) )
                prop->SetImage( tex );
        }
        
        if ( auto tex = static_cast<Image2D*>( instance->GetTexture( "u_NormalTexture" ) ) )
        {
            if ( auto prop = m_MaterialExecutor->GetTexture2DProperty( "u_NormalTexture" ) )
                prop->SetImage( tex );
        }
    }
} // namespace Desert::Graphic