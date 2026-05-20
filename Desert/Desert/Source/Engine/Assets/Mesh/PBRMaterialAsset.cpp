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
            // slot.reset();
        }

        auto loadParam = [&]( const auto& param, TextureAsset::Type type, const glm::vec4& defaultColor )
        {
            if ( param.Texture.has_value() )
            {
                const Common::UUID& handle = param.Texture->Handle;

                if ( !AddTexture( handle, type, defaultColor ) )
                {
                    LOG_WARN( "Failed to load texture for material: {}", handle.ToString() );
                }
            }
            else
            {
                TextureSlot textureSlot{};
                textureSlot.DefaultColor = defaultColor;

                m_TextureSlots[static_cast<size_t>( type )] = textureSlot;
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

    bool PBRMaterialAsset::AddTexture( const Assets::AssetHandle& handle, TextureAsset::Type type,
                                       const glm::vec4& defaultColor )
    {
        auto index = static_cast<size_t>( type );

        // if ( !m_TextureSlots[index] )
        {
            m_TextureSlots[index].TextureHandle = handle;
            m_TextureSlots[index].DefaultColor  = defaultColor;
        }
        return true;
    }

    std::optional<AssetHandle> PBRMaterialAsset::GetTextureHandle( TextureAsset::Type type ) const
    {
        const auto index = static_cast<size_t>( type );

        if ( index >= m_TextureSlots.size() )
        {
            return std::nullopt;
        }

        const auto& slot = m_TextureSlots[index];

        if ( !slot.IsValid() )
        {
            return std::nullopt;
        }

        return slot.TextureHandle;
    }

} // namespace Desert::Assets