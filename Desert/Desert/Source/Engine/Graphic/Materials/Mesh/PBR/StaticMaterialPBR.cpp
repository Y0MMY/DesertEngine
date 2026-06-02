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
        // Property name must match the push constant in Static.glsl.vert
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

        // 1. Update push constants (Transform)
        glm::mat4 transform = instance->GetMat4( "Transform" );
        m_MaterialExecutor->PushConstant( &transform, sizeof( glm::mat4 ) );

        // 2. Pack PBR properties into the uniform block
        PBRUniforms uniforms;
        uniforms.AlbedoColor      = instance->GetVec3( "AlbedoColor", glm::vec3(1.0f) );
        uniforms.AlbedoBlend      = instance->GetFloat( "AlbedoBlend", 1.0f );
        uniforms.MetallicValue    = instance->GetFloat( "MetallicValue", 0.0f );
        uniforms.MetallicBlend    = instance->GetFloat( "MetallicBlend", 1.0f );
        uniforms.RoughnessValue   = instance->GetFloat( "RoughnessValue", 0.5f );
        uniforms.RoughnessBlend   = instance->GetFloat( "RoughnessBlend", 1.0f );
        uniforms.EmissionColor    = instance->GetVec3( "EmissionColor", glm::vec3(0.0f) );
        uniforms.EmissionStrength = instance->GetFloat( "EmissionStrength", 1.0f );
        uniforms.AOValue          = instance->GetFloat( "AOValue", 1.0f );

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "PBRMaterialPropertiesUB" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &uniforms ), sizeof( PBRUniforms ) );
        }

        // 3. Bind textures
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

        // 4. Finalize and apply to GPU
        m_MaterialExecutor->Apply();
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
        // Handled manually in Bind() to avoid top-level property warnings
    }
} // namespace Desert::Graphic