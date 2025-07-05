#include <Engine/Graphic/Materials/MaterialPBR.hpp>

#include <Engine/Graphic/Materials/Material.hpp>

namespace Desert::Graphic
{
    MaterialPBR::MaterialPBR( const std::shared_ptr<Assets::MaterialAsset>& baseAsset )
         : m_BaseMaterial( baseAsset )
    {
        // Create a new material instance based on the base asset
        if ( const auto& baseMaterial = m_BaseMaterial.lock(); baseMaterial )
        {
            InheritBaseMaterialProperties();
        }

        const auto& shader = Graphic::ShaderLibrary::Get( "StaticPBR.glsl", {} );

        m_Material = Graphic::Material::Create( "MaterialPBR", shader.GetValue() );

        m_GlobalUB           = std::make_unique<Models::GlobalData>( m_Material );
        m_PBRUB              = std::make_unique<Models::PBR::PBRMaterial>( m_Material );
        m_PBRTextures        = std::make_unique<Models::PBR::PBRMaterialTexture>( m_Material );
        m_MaterialProperties = std::make_unique<Models::PBR::MaterialProperties>( m_Material );
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
                m_AlbedoColor = albedoSlot->DefaultColor;
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
    void MaterialPBR::SetTexture( Assets::TextureAsset::Type type, std::shared_ptr<Assets::TextureAsset> texture )
    {
        m_Textures[type] = std::move( texture );
        MarkDirty();
    }

    void MaterialPBR::RemoveTexture( Assets::TextureAsset::Type type )
    {
        if ( m_Textures.erase( type ) > 0 )
        {
            MarkDirty();
        }
    }

    bool MaterialPBR::HasTexture( Assets::TextureAsset::Type type ) const
    {
        return m_Textures.find( type ) != m_Textures.end();
    }

    std::shared_ptr<Assets::TextureAsset> MaterialPBR::GetTexture( Assets::TextureAsset::Type type ) const
    {
        auto it = m_Textures.find( type );
        return it != m_Textures.end() ? it->second : nullptr;
    }

    std::shared_ptr<Graphic::Texture2D> MaterialPBR::GetFinalTexture( Assets::TextureAsset::Type type ) const
    {
        // First check instance textures
        if ( auto it = m_Textures.find( type ); it != m_Textures.end() && it->second->IsReadyForUse() )
        {
            return it->second->GetTexture();
        }

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

    void MaterialPBR::UpdateRenderParameters( const Core::Camera&                            camera,
                                              const std::optional<Models::PBR::PBRTextures>& pbrTextures )
    {
        const VP vp{ .ViewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix(),
                     .Transform      = glm::mat4( 1.0 ) };

        m_GlobalUB->UpdateUBGlobal( Models::GlobalUB{ .CameraPosition = camera.GetPosition() } );
        m_PBRUB->UpdatePBR( {} );
        m_PBRTextures->UpdatePBR( pbrTextures );
        m_Material->PushConstant( &vp, sizeof( vp ) );

        // Here we would update the actual Material object with our parameters
        // This is where we'd connect to your Material class

        //{
        //    // Update uniform buffer properties
        //    Models::PBR::MaterialPropertiesUB props;
        //    props.AlbedoColor      = m_AlbedoColor;
        //    props.AlbedoBlend      = m_AlbedoBlend;
        //    props.MetallicValue    = m_MetallicValue;
        //    props.MetallicBlend    = m_MetallicBlend;
        //    props.RoughnessValue   = m_RoughnessValue;
        //    props.RoughnessBlend   = m_RoughnessBlend;
        //    props.EmissionColor    = m_EmissionColor;
        //    props.EmissionStrength = m_EmissionStrength;
        //    props.AOValue          = m_AOValue;

        //    m_MaterialProperties->Update( props );

        //    // Update textures
        //    auto updateTexture = [&]( Assets::TextureAsset::Type type, const std::string& name )
        //    {
        //        if ( auto texture = GetFinalTexture( type ) )
        //        {
        //            if ( auto texProp = m_Material->GetTexture2DProperty( name ) )
        //            {
        //                texProp->SetTexture( texture );
        //            }
        //        }
        //    };

        //    updateTexture( Assets::TextureAsset::Type::Albedo, "albedoMap" );
        //    updateTexture( Assets::TextureAsset::Type::Normal, "normalMap" );
        //    updateTexture( Assets::TextureAsset::Type::Metallic, "metallicMap" );
        //    updateTexture( Assets::TextureAsset::Type::Roughness, "roughnessMap" );
        //    updateTexture( Assets::TextureAsset::Type::AO, "aoMap" );
        //    updateTexture( Assets::TextureAsset::Type::Emissive, "emissiveMap" );
        //}

        m_ParametersDirty = false;
    }

} // namespace Desert::Graphic