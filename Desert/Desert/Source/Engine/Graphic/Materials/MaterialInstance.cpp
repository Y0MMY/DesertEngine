#include <Engine/Graphic/Materials/MaterialInstance.hpp>

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    MaterialInstance::MaterialInstance( std::shared_ptr<Assets::MaterialAsset> baseAsset )
         : m_BaseMaterial( std::move( baseAsset ) )
    {
        // Create a new material instance based on the base asset
        if ( m_BaseMaterial )
        {
            InheritBaseMaterialProperties();
        }

        const auto& shader = Graphic::ShaderLibrary::Get( "StaticPBR.glsl", {} );

        m_Material = Graphic::Material::Create( "MaterialInstance", shader.GetValue() );
    }

    void MaterialInstance::SetBaseMaterial( std::shared_ptr<Assets::MaterialAsset> asset )
    {
        if ( m_BaseMaterial != asset )
        {
            m_BaseMaterial = std::move( asset );
            InheritBaseMaterialProperties();
            MarkDirty();
        }
    }

    void MaterialInstance::InheritBaseMaterialProperties()
    {
        if ( !m_BaseMaterial )
            return;

        // Inherit properties from base material if they haven't been overridden
        if ( m_AlbedoBlend == 1.0f ) // Default value means not overridden
        {
            if ( auto albedoSlot = m_BaseMaterial->GetTextureSlot( Assets::TextureAsset::Type::Albedo ) )
            {
                m_AlbedoColor = albedoSlot->DefaultColor;
            }
        }

        // Similar inheritance for other properties...
    }

    // Albedo properties
    void MaterialInstance::SetAlbedo( const glm::vec3& color, float textureBlend )
    {
        if ( m_AlbedoColor != color || m_AlbedoBlend != textureBlend )
        {
            m_AlbedoColor = color;
            m_AlbedoBlend = textureBlend;
            MarkDirty();
        }
    }

    // Metallic properties
    void MaterialInstance::SetMetallic( float value, float textureBlend )
    {
        if ( m_MetallicValue != value || m_MetallicBlend != textureBlend )
        {
            m_MetallicValue = value;
            m_MetallicBlend = textureBlend;
            MarkDirty();
        }
    }

    // Roughness properties
    void MaterialInstance::SetRoughness( float value, float textureBlend )
    {
        if ( m_RoughnessValue != value || m_RoughnessBlend != textureBlend )
        {
            m_RoughnessValue = value;
            m_RoughnessBlend = textureBlend;
            MarkDirty();
        }
    }

    // Emission properties
    void MaterialInstance::SetEmission( const glm::vec3& color, float strength )
    {
        if ( m_EmissionColor != color || m_EmissionStrength != strength )
        {
            m_EmissionColor    = color;
            m_EmissionStrength = strength;
            MarkDirty();
        }
    }

    // Ambient Occlusion properties
    void MaterialInstance::SetAO( float value )
    {
        if ( m_AOValue != value )
        {
            m_AOValue = value;
            MarkDirty();
        }
    }

    // Texture operations
    void MaterialInstance::SetTexture( Assets::TextureAsset::Type            type,
                                       std::shared_ptr<Assets::TextureAsset> texture )
    {
        m_Textures[type] = std::move( texture );
        MarkDirty();
    }

    void MaterialInstance::RemoveTexture( Assets::TextureAsset::Type type )
    {
        if ( m_Textures.erase( type ) > 0 )
        {
            MarkDirty();
        }
    }

    bool MaterialInstance::HasTexture( Assets::TextureAsset::Type type ) const
    {
        return m_Textures.find( type ) != m_Textures.end();
    }

    std::shared_ptr<Assets::TextureAsset> MaterialInstance::GetTexture( Assets::TextureAsset::Type type ) const
    {
        auto it = m_Textures.find( type );
        return it != m_Textures.end() ? it->second : nullptr;
    }

    std::shared_ptr<Graphic::Texture2D> MaterialInstance::GetFinalTexture( Assets::TextureAsset::Type type ) const
    {
        // First check instance textures
        if ( auto it = m_Textures.find( type ); it != m_Textures.end() && it->second->IsReadyForUse() )
        {
            return it->second->GetTexture();
        }

        // Then check base material
        if ( m_BaseMaterial )
        {
            return m_BaseMaterial->GetTexture( type );
        }

        return nullptr;
    }

    bool MaterialInstance::HasFinalTexture( Assets::TextureAsset::Type type ) const
    {
        return GetFinalTexture( type ) != nullptr;
    }

    void MaterialInstance::UpdateRenderParameters( bool forceUpdate )
    {
        if ( !( m_ParametersDirty || forceUpdate ) )
            return;

        // Here we would update the actual Material object with our parameters
        // This is where we'd connect to your Material class

        {
            // Update uniform buffer properties
            if ( auto ubo = m_Material->GetUniformBufferProperty( "MaterialProperties" ) )
            {
                struct MaterialProperties
                {
                    glm::vec3 albedoColor;
                    float     albedoBlend;
                    float     metallicValue;
                    float     metallicBlend;
                    float     roughnessValue;
                    float     roughnessBlend;
                    glm::vec3 emissionColor;
                    float     emissionStrength;
                    float     aoValue;
                } props;

                props.albedoColor      = m_AlbedoColor;
                props.albedoBlend      = m_AlbedoBlend;
                props.metallicValue    = m_MetallicValue;
                props.metallicBlend    = m_MetallicBlend;
                props.roughnessValue   = m_RoughnessValue;
                props.roughnessBlend   = m_RoughnessBlend;
                props.emissionColor    = m_EmissionColor;
                props.emissionStrength = m_EmissionStrength;
                props.aoValue          = m_AOValue;

                ubo->SetData( &props, sizeof( props ) );
            }

            // Update textures
            auto updateTexture = [&]( Assets::TextureAsset::Type type, const std::string& name )
            {
                if ( auto texture = GetFinalTexture( type ) )
                {
                    if ( auto texProp = m_Material->GetTexture2DProperty( name ) )
                    {
                        texProp->SetTexture( texture );
                    }
                }
            };

            updateTexture( Assets::TextureAsset::Type::Albedo, "albedoMap" );
            updateTexture( Assets::TextureAsset::Type::Normal, "normalMap" );
            updateTexture( Assets::TextureAsset::Type::Metallic, "metallicMap" );
            updateTexture( Assets::TextureAsset::Type::Roughness, "roughnessMap" );
            updateTexture( Assets::TextureAsset::Type::AO, "aoMap" );
            updateTexture( Assets::TextureAsset::Type::Emissive, "emissiveMap" );

            m_Material->Apply();
        }

        m_ParametersDirty = false;
    }

} // namespace Desert::Graphic