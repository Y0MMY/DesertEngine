#include "MaterialPBR.hpp"

#include <Engine/Graphic/Materials/MaterialExecutor.hpp>

namespace Desert::Graphic
{
#define MAKE_RESOURCE( type, var ) var = std::make_unique<type>( m_MaterialExecutor )

    MaterialPBR::MaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset )
         : Material( "MaterialPBR", "StaticPBR.glsl" ), m_BaseMaterial( baseAsset )
    {
        // Create a new material instance based on the base asset
        if ( const auto& baseMaterial = m_BaseMaterial.lock(); baseMaterial )
        {
            InheritBaseMaterialProperties();
        }
        MAKE_RESOURCE( Models::Light::DirectionLightUB, m_DirectionLightUB );
        MAKE_RESOURCE( Models::Light::PointLightUB, m_PointLightUB );
        MAKE_RESOURCE( Models::Light::LightsMetadataUB, m_LightsMetadataUB);
        MAKE_RESOURCE( Models::GlobalData, m_GlobalUB );
        MAKE_RESOURCE( Models::PBR::PBRMaterialTexture, m_PBRTextures );
        MAKE_RESOURCE( Models::PBR::MaterialPBRUB, m_MaterialProperties );
    }

    void MaterialPBR::InheritBaseMaterialProperties()
    {
        const auto& baseMaterial = m_BaseMaterial.lock();
        if ( !baseMaterial )
            return;

        // Inherit properties from base material if they haven't been overridden
        if ( m_AlbedoBlend == 1.0f ) // Default value means not overridden
        {
            if ( auto albedoSlot = baseMaterial->GetTextureSlot( Assets::TextureAsset::Type::Albedo ) )
            {
                m_AlbedoColor = albedoSlot->get().DefaultColor;
            }
        }

        // Similar inheritance for other properties...
    }

    // Albedo properties
    void MaterialPBR::SetAlbedo( const glm::vec3& color, float textureBlend )
    {
        if ( m_AlbedoColor != color || m_AlbedoBlend != textureBlend )
        {
            m_AlbedoColor = color;
            m_AlbedoBlend = textureBlend;
            MarkDirty();
        }
    }

    // Metallic properties
    void MaterialPBR::SetMetallic( float value, float textureBlend )
    {
        if ( m_MetallicValue != value || m_MetallicBlend != textureBlend )
        {
            m_MetallicValue = value;
            m_MetallicBlend = textureBlend;
            MarkDirty();
        }
    }

    // Roughness properties
    void MaterialPBR::SetRoughness( float value, float textureBlend )
    {
        if ( m_RoughnessValue != value || m_RoughnessBlend != textureBlend )
        {
            m_RoughnessValue = value;
            m_RoughnessBlend = textureBlend;
            MarkDirty();
        }
    }

    // Emission properties
    void MaterialPBR::SetEmission( const glm::vec3& color, float strength )
    {
        if ( m_EmissionColor != color || m_EmissionStrength != strength )
        {
            m_EmissionColor    = color;
            m_EmissionStrength = strength;
            MarkDirty();
        }
    }

    // Ambient Occlusion properties
    void MaterialPBR::SetAO( float value )
    {
        if ( m_AOValue != value )
        {
            m_AOValue = value;
            MarkDirty();
        }
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

    struct VP
    {
        glm::mat4 ViewProjection;
        glm::mat4 Transform;
    };

    void MaterialPBR::Bind( const UpdateMaterialPBRInfo& data )
    {
        const VP vp{ .ViewProjection = data.Camera->GetProjectionMatrix() * data.Camera->GetViewMatrix(),
                     .Transform      = data.MeshTransform };

        m_GlobalUB->Update( Models::GlobalUB{ .CameraPosition = data.Camera->GetPosition() } );
        m_PBRTextures->UpdatePBR( data.PbrTextures );
        m_MaterialExecutor->PushConstant( &vp, sizeof( vp ) );

        UpdatePointLight( data.PointLights );
        UpdateDirectionLight( data.DirectionLights );

        UpdateLightsMetadata( data.PointLights, data.DirectionLights );

        // Here we would update the actual Material object with our parameters
        // This is where we'd connect to your Material class

        {
            // Update uniform buffer properties
            Models::PBR::PBRMaterialPropertiesUB props;
            props.AlbedoColor      = m_AlbedoColor;
            props.AlbedoBlend      = m_AlbedoBlend;
            props.MetallicValue    = m_MetallicValue;
            props.MetallicBlend    = m_MetallicBlend;
            props.RoughnessValue   = m_RoughnessValue;
            props.RoughnessBlend   = m_RoughnessBlend;
            props.EmissionColor    = m_EmissionColor;
            props.EmissionStrength = m_EmissionStrength;
            props.AOValue          = m_AOValue;

            m_MaterialProperties->Update( props );

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
        m_PointLightUB->Update( pointLights );
    }

    void MaterialPBR::UpdateDirectionLight( const std::vector<DirectionLight>& directionLights )
    {
        m_DirectionLightUB->Update( directionLights );
    }

    void MaterialPBR::UpdateLightsMetadata( const std::vector<PointLight>&     pointLights,
                                            const std::vector<DirectionLight>& directionLights )
    {
        m_LightsMetadataUB->Update( { .DirectionLightCount = static_cast<uint32_t>( directionLights.size() ),
                                      .PointLightCount     = static_cast<uint32_t>( pointLights.size() ) } );
    }

} // namespace Desert::Graphic