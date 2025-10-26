#include "MaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

#include <rflcpp/rfl/json/write.hpp>

#include <Engine/Graphic/Renderer.hpp>

namespace Desert::Graphic
{
#define MAKE_RESOURCE( type, var ) var = std::make_unique<type>( type{} )

    MaterialPBR::MaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset )
         : Material( "MaterialPBR", "StaticPBR.glsl" ), m_BaseMaterial( baseAsset )
    {
        // Create a new material instance based on the base asset
        if ( const auto& baseMaterial = m_BaseMaterial.lock(); baseMaterial )
        {
            InheritBaseMaterialProperties();
        }
        MAKE_RESOURCE( Models::Light::DirectionLightsUB, m_DirectionLightUB );
        MAKE_RESOURCE( Models::Light::PointLightsUB, m_PointLightUB );
        MAKE_RESOURCE( Models::Light::LightsMetadata, m_LightsMetadataUB );
        MAKE_RESOURCE( Models::CameraDataUB, m_CameraData );
        MAKE_RESOURCE( Models::PBR::PBRTextures, m_PBRTextures );
        MAKE_RESOURCE( Models::PBR::PBRMaterialPropertiesUB, m_MaterialProperties );

        m_MaterialProperties->for_each_field(
             [this]( const auto& fieldName, auto& rflValue)
             {
                 using ValueType    = decltype( rflValue.get() );

                 if constexpr ( std::is_same_v<std::decay_t<ValueType>, float> )
                 {
                     rflValue.SetValue( 1.0f );
                 }
             } );

        InitializeUniforms();
    }

    void MaterialPBR::InheritBaseMaterialProperties()
    {
      
    }

    // Albedo properties
    void MaterialPBR::SetAlbedo( const glm::vec3& color, float textureBlend )
    {
        
    }

    // Metallic properties
    void MaterialPBR::SetMetallic( float value, float textureBlend )
    {
        
    }

    // Roughness properties
    void MaterialPBR::SetRoughness( float value, float textureBlend )
    {
        
    }

    // Emission properties
    void MaterialPBR::SetEmission( const glm::vec3& color, float strength )
    {
       
    }

    // Ambient Occlusion properties
    void MaterialPBR::SetAO( float value )
    {
      
    }

    // Texture operations
    void MaterialPBR::SetNewTexture( Assets::TextureAsset::Type type, const Common::Filepath& path )
    {
        const auto& baseMaterial = m_BaseMaterial.lock();
        if ( !baseMaterial )
            return;

        const bool resultNewTexture = baseMaterial->AddTexture( path, type );
        if ( !resultNewTexture )
        {
            return;
        }

        MarkDirty();
    }

    void MaterialPBR::RemoveTexture( Assets::TextureAsset::Type type )
    {
        /* if ( m_Textures.erase( type ) > 0 )
         {
             MarkDirty();
         }*/
    }

    bool MaterialPBR::HasTexture( Assets::TextureAsset::Type type ) const
    {
        return false; // m_Textures.find(type) != m_Textures.end();
    }

    std::shared_ptr<Assets::TextureAsset> MaterialPBR::GetTexture( Assets::TextureAsset::Type type ) const
    {
        /* auto it = m_Textures.find( type );
         return it != m_Textures.end() ? it->second : nullptr;*/

        return nullptr;
    }

    std::shared_ptr<Graphic::Texture2D> MaterialPBR::GetFinalTexture( Assets::TextureAsset::Type type ) const
    {
        const auto& baseMaterial = m_BaseMaterial.lock();
        // Then check base material
        if ( baseMaterial )
        {
            return baseMaterial->GetTexture( type );
        }

        return nullptr;
    }

    bool MaterialPBR::HasFinalTexture( Assets::TextureAsset::Type type ) const
    {
        return GetFinalTexture( type ) != nullptr;
    }

    void MaterialPBR::Bind( const UpdateMaterialPBRInfo& data )
    {
        UpdateCamera( data.Camera.get() );
        UpdatePBRTextures( data.PbrTextures );
        m_MaterialExecutor->PushConstant( &data.MeshTransform, sizeof( glm::mat4 ) );

        UpdatePointLight( data.PointLights );
        UpdateDirectionLight( data.DirectionLights );

        UpdateLightsMetadata( data.PointLights, data.DirectionLights );

        // Here we would update the actual Material object with our parameters
        // This is where we'd connect to your Material class

        {

            SetUniformValue( *m_MaterialProperties );
            SyncToGPU( Models::PBR::PBRMaterialPropertiesUB::shader_UB_name );

            // Update textures
            auto updateTexture = [&]( Assets::TextureAsset::Type type, const std::string& name )
            {
                auto texture = GetFinalTexture( type );
                {
                    if ( auto texProp = m_MaterialExecutor->GetTexture2DProperty( name ) )
                    {
                        texProp->SetTexture( texture );
                    }
                }
            };

            updateTexture( Assets::TextureAsset::Type::Albedo, "u_AlbedoTexture" );
            updateTexture( Assets::TextureAsset::Type::Normal, "u_NormalTexture" );
            updateTexture( Assets::TextureAsset::Type::Metallic, "metallicMap" );
            updateTexture( Assets::TextureAsset::Type::Roughness, "roughnessMap" );
            updateTexture( Assets::TextureAsset::Type::AO, "aoMap" );
            updateTexture( Assets::TextureAsset::Type::Emissive, "emissiveMap" );
        }

        m_ParametersDirty = false;
    }

    void MaterialPBR::UpdatePointLight( const std::vector<PointLight>& pointLights )
    {
        m_PointLightUB->lights = pointLights;

        SetUniformValue( *m_PointLightUB );
        SyncToGPU( Models::Light::PointLightsUB::shader_UB_name );
    }

    void MaterialPBR::UpdateDirectionLight( const std::vector<DirectionLight>& directionLights )
    {
        /* Models::Light::DirectionLightsUB dirUB;

         dirUB.directionLights = directionLights;
         const auto str        = rfl::json::write( dirUB );
         m_DirectionLightUB->Update( dirUB );*/
    }

    void MaterialPBR::UpdateLightsMetadata( const std::vector<PointLight>&     pointLights,
                                            const std::vector<DirectionLight>& directionLights )
    {
        m_LightsMetadataUB->DirectionLightCount = directionLights.size();
        m_LightsMetadataUB->PointLightCount     = pointLights.size();

        SetUniformValue( *m_LightsMetadataUB );
        SyncToGPU( Models::Light::LightsMetadata::shader_UB_name );
    }

    void MaterialPBR::UpdateCamera( const Core::Camera* camera )
    {
        m_CameraData->View       = camera->GetViewMatrix();
        m_CameraData->Projection = camera->GetProjectionMatrix();
        m_CameraData->CameraPos  = camera->GetPosition();

        SetUniformValue( *m_CameraData );
        SyncToGPU( Models::CameraDataUB::shader_UB_name );
    }

    void MaterialPBR::UpdatePBRTextures( const std::optional<Models::PBR::PBRTextures>& pbrTextures )
    {
        if ( pbrTextures )
        {
            SetUniformValue( pbrTextures.value() );
            SyncToGPU( Models::PBR::PBRTextures::shader_UB_name );
        }
        else
        {
            //SetUniformValue(Models::PBR::PBRTextures{});
            //SyncToGPU( Models::PBR::PBRTextures::shader_UB_name );
        }
    }

} // namespace Desert::Graphic