#include "PBRMaterialAsset.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>
#include <Engine/Assets/Serialization/Material.hpp>

#include <Common/Utilities/FileSystem.hpp>

namespace Desert::Assets
{

    PBRMaterialAsset::PBRMaterialAsset( AssetPriority priority, const Common::Filepath& filepath )
         : MaterialAsset( priority, filepath, AssetTypeID::Material )
    {
    }

    Common::BoolResultStr PBRMaterialAsset::Load()
    {
        auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto dataReflected = rfl::json::read<Serialization::MaterialAssetData>( raw );

        if ( !dataReflected.has_value() )
        {
            return Common::MakeError( dataReflected.error().what() );
        }

        const auto& data = dataReflected.value();

        for ( auto& slot : m_TextureSlots )
        {
            slot.reset();
        }

        auto loadParam = [&]( const auto& param, TextureAsset::Type type, const glm::vec4& defaultColor )
        {
            if ( param.Texture.has_value() )
            {
                const auto& path = param.Texture->Path;

                if ( !AddTexture( path, type, defaultColor ) )
                {
                    LOG_WARN( "Failed to load texture for material: {}", path );
                }
            }
            else
            {
                auto textureSlot          = std::make_unique<TextureSlot>();
                textureSlot->DefaultColor = defaultColor;

                m_TextureSlots[static_cast<size_t>( type )] = std::move( textureSlot );
            }
        };

        loadParam( data.Albedo, TextureAsset::Type::Albedo, data.Albedo.Value );

        loadParam( data.Metallic, TextureAsset::Type::Metallic, glm::vec4( data.Metallic.Value ) );

        loadParam( data.Roughness, TextureAsset::Type::Roughness, glm::vec4( data.Roughness.Value ) );

        loadParam( data.AO, TextureAsset::Type::AO, glm::vec4( data.AO.Value ) );

        loadParam( data.Emissive, TextureAsset::Type::Emissive, glm::vec4( data.Emissive.Value, 1.0f ) );

        m_MaterialUUID = data.MaterialHandle;

        m_ReadyForUse = true;

        return BOOLSUCCESS;
    }

    Common::BoolResultStr PBRMaterialAsset::Unload()
    {
        return BOOLSUCCESS;
    }

    std::optional<std::reference_wrapper<const PBRMaterialAsset::TextureSlot>>
    PBRMaterialAsset::GetTextureSlot( TextureAsset::Type type ) const
    {
        const auto& slot = m_TextureSlots[static_cast<size_t>( type )];
        if ( slot )
            return std::cref( *slot );
        return std::nullopt;
    }

    TextureAsset* PBRMaterialAsset::GetTexture( TextureAsset::Type type ) const
    {
        if ( auto slot = GetTextureSlot( type ) )
        {
            if ( slot->get().Texture && slot->get().Texture->IsReadyForUse() )
            {
                return slot->get().Texture.get();
            }
        }
        return nullptr;
    }

    bool PBRMaterialAsset::AddTexture( const Common::Filepath& filepath, TextureAsset::Type type,
                                       const glm::vec4& defaultColor )
    {
        if ( m_TextureSlots[static_cast<size_t>( type )] )
        {
            LOG_WARN( "Texture slot for type {} is already occupied", static_cast<int>( type ) );
            return false;
        }

        auto textureSlot          = std::make_unique<TextureSlot>();
        textureSlot->Texture      = std::make_unique<TextureAsset>( AssetPriority::Low, filepath, type );
        textureSlot->DefaultColor = defaultColor;

        if ( !textureSlot->Texture->Load() )
        {
            LOG_WARN( "Failed to load texture: {}", filepath.string() );
            return false;
        }

        m_TextureSlots[static_cast<size_t>( type )] = std::move( textureSlot );
        return true;
    }

    bool PBRMaterialAsset::CopyFrom( const MaterialAsset& source )
    {

        return true;
    }

} // namespace Desert::Assets