#include "StaticMaterialPBR.hpp"
#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
    StaticMaterialPBR::StaticMaterialPBR() : Material( "PBRMaterial", "StaticMeshPBR" )
    {
        // Set default parameters
        SetDefaultParameter( "AlbedoColor", glm::vec3( 1.0f ), MaterialPropertyType::Vec3 );
        SetDefaultParameter( "MetallicFactor", 0.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "RoughnessFactor", 0.5f, MaterialPropertyType::Float );
        SetDefaultParameter( "EmissiveIntensity", 1.0f, MaterialPropertyType::Float );
        SetDefaultParameter( "UseAlbedoTexture", 0, MaterialPropertyType::Int );
        SetDefaultParameter( "UseNormalTexture", 0, MaterialPropertyType::Int );
        SetDefaultParameter( "UseMetallicTexture", 0, MaterialPropertyType::Int );
        SetDefaultParameter( "UseRoughnessTexture", 0, MaterialPropertyType::Int );
        SetDefaultParameter( "UseAOTexture", 0, MaterialPropertyType::Int );
        SetDefaultParameter( "UseEmissiveTexture", 0, MaterialPropertyType::Int );
    }

    void StaticMaterialPBR::SetAlbedo( MaterialInstance* instance, const Image2D* texture, const glm::vec3& color )
    {
        instance->SetVec3( "AlbedoColor", color );
        if ( texture )
        {
            instance->SetTexture( "AlbedoTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseAlbedoTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseAlbedoTexture", 0 );
        }
    }

    void StaticMaterialPBR::SetNormalMap( MaterialInstance* instance, const Image2D* texture )
    {
        if ( texture )
        {
            instance->SetTexture( "NormalTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseNormalTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseNormalTexture", 0 );
        }
    }

    void StaticMaterialPBR::SetMetallic( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "MetallicFactor", value );
        if ( texture )
        {
            instance->SetTexture( "MetallicTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseMetallicTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseMetallicTexture", 0 );
        }
    }

    void StaticMaterialPBR::SetRoughness( MaterialInstance* instance, float value, const Image2D* texture )
    {
        instance->SetFloat( "RoughnessFactor", value );
        if ( texture )
        {
            instance->SetTexture( "RoughnessTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseRoughnessTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseRoughnessTexture", 0 );
        }
    }

    void StaticMaterialPBR::SetAmbientOcclusion( MaterialInstance* instance, const Image2D* texture )
    {
        if ( texture )
        {
            instance->SetTexture( "AOTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseAOTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseAOTexture", 0 );
        }
    }

    void StaticMaterialPBR::SetEmissive( MaterialInstance* instance, const Image2D* texture, float intensity )
    {
        instance->SetFloat( "EmissiveIntensity", intensity );
        if ( texture )
        {
            instance->SetTexture( "EmissiveTexture", const_cast<Image2D*>( texture ) );
            instance->SetInt( "UseEmissiveTexture", 1 );
        }
        else
        {
            instance->SetInt( "UseEmissiveTexture", 0 );
        }
    }

    void StaticMaterialPBR::Bind( const MaterialInstance* instance )
    {
        if ( !m_MaterialExecutor )
            return;

        // Apply instance properties first (calls SetFloat, SetInt, etc. which use the property system)
        Material::Bind( instance );

        // Update PBR specific uniforms if dirty
        if ( m_UniformsDirty )
        {
            // Update transform
            if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "ModelMatrix" ) )
            {
                prop->SetRawData( reinterpret_cast<const std::byte*>( &m_TransformMatrix ), sizeof( glm::mat4 ) );
            }

            m_UniformsDirty = false;
        }

        // Apply all pending changes to GPU
        m_MaterialExecutor->Apply();
    }

    void StaticMaterialPBR::OnBind( MaterialInstance* instance )
    {
        // Update uniforms from instance
        m_CurrentUniforms.AlbedoColor         = instance->GetVec3( "AlbedoColor" );
        m_CurrentUniforms.MetallicFactor      = instance->GetFloat( "MetallicFactor" );
        m_CurrentUniforms.RoughnessFactor     = instance->GetFloat( "RoughnessFactor" );
        m_CurrentUniforms.EmissiveIntensity   = instance->GetFloat( "EmissiveIntensity" );
        m_CurrentUniforms.UseAlbedoTexture    = instance->GetInt( "UseAlbedoTexture" );
        m_CurrentUniforms.UseNormalTexture    = instance->GetInt( "UseNormalTexture" );
        m_CurrentUniforms.UseMetallicTexture  = instance->GetInt( "UseMetallicTexture" );
        m_CurrentUniforms.UseRoughnessTexture = instance->GetInt( "UseRoughnessTexture" );
        m_CurrentUniforms.UseAOTexture        = instance->GetInt( "UseAOTexture" );
        m_CurrentUniforms.UseEmissiveTexture  = instance->GetInt( "UseEmissiveTexture" );

        // Get texture pointers from instance
        m_AlbedoTexture    = static_cast<const Image2D*>( instance->GetTexture( "AlbedoTexture" ) );
        m_NormalTexture    = static_cast<const Image2D*>( instance->GetTexture( "NormalTexture" ) );
        m_MetallicTexture  = static_cast<const Image2D*>( instance->GetTexture( "MetallicTexture" ) );
        m_RoughnessTexture = static_cast<const Image2D*>( instance->GetTexture( "RoughnessTexture" ) );
        m_AOTexture        = static_cast<const Image2D*>( instance->GetTexture( "AOTexture" ) );
        m_EmissiveTexture  = static_cast<const Image2D*>( instance->GetTexture( "EmissiveTexture" ) );

        // Apply PBR uniform values through the property system
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "PBRData" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &m_CurrentUniforms ), sizeof( PBRUniforms ) );
        }

        // Bind textures through MaterialExecutor properties
        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "AlbedoTexture" ) )
        {
            if ( m_AlbedoTexture && m_CurrentUniforms.UseAlbedoTexture )
                texProp->SetImage( m_AlbedoTexture );
        }

        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "NormalTexture" ) )
        {
            if ( m_NormalTexture && m_CurrentUniforms.UseNormalTexture )
                texProp->SetImage( m_NormalTexture );
        }

        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "MetallicTexture" ) )
        {
            if ( m_MetallicTexture && m_CurrentUniforms.UseMetallicTexture )
                texProp->SetImage( m_MetallicTexture );
        }

        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "RoughnessTexture" ) )
        {
            if ( m_RoughnessTexture && m_CurrentUniforms.UseRoughnessTexture )
                texProp->SetImage( m_RoughnessTexture );
        }

        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "AOTexture" ) )
        {
            if ( m_AOTexture && m_CurrentUniforms.UseAOTexture )
                texProp->SetImage( m_AOTexture );
        }

        if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( "EmissiveTexture" ) )
        {
            if ( m_EmissiveTexture && m_CurrentUniforms.UseEmissiveTexture )
                texProp->SetImage( m_EmissiveTexture );
        }

        m_UniformsDirty = true;
    }

    void StaticMaterialPBR::UpdateCamera( const Core::Camera* camera )
    {
        if ( !m_MaterialExecutor || !camera )
            return;

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "ViewMatrix" ) )
        {
            const auto& viewMat = camera->GetViewMatrix();
            prop->SetRawData( reinterpret_cast<const std::byte*>( &viewMat ), sizeof( glm::mat4 ) );
        }

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "ProjectionMatrix" ) )
        {
            const auto& projMat = camera->GetProjectionMatrix();
            prop->SetRawData( reinterpret_cast<const std::byte*>( &projMat ), sizeof( glm::mat4 ) );
        }

        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "CameraPosition" ) )
        {
            const auto& camPos = camera->GetPosition();
            prop->SetRawData( reinterpret_cast<const std::byte*>( &camPos ), sizeof( glm::vec3 ) );
        }
    }

    void StaticMaterialPBR::UpdateLights( const ShaderProtocols::PointLight&     pointLights,
                                    const ShaderProtocols::DirectionLight& dirLights )
    {
        if ( !m_MaterialExecutor )
            return;

        // Update directional light
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "DirectionLight" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &dirLights ),
                              sizeof( ShaderProtocols::DirectionLight ) );
        }

        // Update point lights
        if ( auto prop = m_MaterialExecutor->GetUniformBufferProperty( "PointLights" ) )
        {
            prop->SetRawData( reinterpret_cast<const std::byte*>( &pointLights ),
                              sizeof( ShaderProtocols::PointLight ) );
        }
    }

    void StaticMaterialPBR::UpdateTransform( const glm::mat4& modelMatrix )
    {
        m_TransformMatrix = modelMatrix;
        m_UniformsDirty   = true;
    }
} // namespace Desert::Graphic