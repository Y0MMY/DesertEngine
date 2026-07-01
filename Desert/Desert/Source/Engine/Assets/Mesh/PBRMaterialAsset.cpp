#include "PBRMaterialAsset.hpp"

#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <Engine/Assets/Serialization/Material.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <algorithm>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{
    namespace
    {
        // Converts the legacy MaterialAssetData (.mat written by the old cooker) into the reflected
        // PBRMaterialData so existing cooked assets keep working; re-saving rewrites them in the new
        // flat format.
        PBRMaterialData FromLegacy( const Serialization::MaterialAssetData& legacy )
        {
            PBRMaterialData d;
            d.AlbedoColor     = legacy.Albedo.Value;
            d.MetallicFactor  = legacy.Metallic.Value;
            d.RoughnessFactor = legacy.Roughness.Value;

            // Old cooker sometimes wrote garbage AO — keep it only if sane, else default to 1.
            const float ao = legacy.AO.Value;
            d.AOStrength     = ( ao >= 0.0f && ao <= 1.0f ) ? ao : 1.0f;
            d.EmissiveColor  = glm::vec4( legacy.Emissive.Value, 1.0f );

            if ( legacy.Albedo.Texture )    d.AlbedoTexture    = legacy.Albedo.Texture->Handle;
            if ( legacy.Metallic.Texture )  d.MetallicTexture  = legacy.Metallic.Texture->Handle;
            if ( legacy.Roughness.Texture ) d.RoughnessTexture = legacy.Roughness.Texture->Handle;
            if ( legacy.AO.Texture )        d.AOTexture        = legacy.AO.Texture->Handle;
            if ( legacy.Emissive.Texture )  d.EmissiveTexture  = legacy.Emissive.Texture->Handle;
            // legacy format has no normal-map slot.
            return d;
        }
    } // namespace

    PBRMaterialAsset::PBRMaterialAsset( AssetPriority priority, const Common::Filepath& filepath )
         : MaterialAsset( priority, filepath, AssetTypeID::Material )
    {
    }

    const Assets::AssetHandle* PBRMaterialAsset::HandleForType( TextureAsset::Type type ) const
    {
        switch ( type )
        {
            case TextureAsset::Type::Albedo:    return &m_Data.AlbedoTexture;
            case TextureAsset::Type::Normal:    return &m_Data.NormalTexture;
            case TextureAsset::Type::Metallic:  return &m_Data.MetallicTexture;
            case TextureAsset::Type::Roughness: return &m_Data.RoughnessTexture;
            case TextureAsset::Type::AO:        return &m_Data.AOTexture;
            case TextureAsset::Type::Emissive:  return &m_Data.EmissiveTexture;
            default:                            return nullptr;
        }
    }

    Assets::AssetHandle* PBRMaterialAsset::HandleForType( TextureAsset::Type type )
    {
        return const_cast<Assets::AssetHandle*>(
             static_cast<const PBRMaterialAsset*>( this )->HandleForType( type ) );
    }

    Common::BoolResultStr PBRMaterialAsset::Load()
    {
        const auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        if ( raw.empty() )
        {
            // New / empty material — start from reflected defaults so it can be edited and re-saved.
            m_Data        = PBRMaterialData{};
            m_ReadyForUse = true;
            return BOOLSUCCESS;
        }

        // Current flat format.
        if ( const auto parsed = rfl::json::read<PBRMaterialData>( raw ); parsed.has_value() )
        {
            m_Data = parsed.value();
            // Adopt the persisted stable id as the external handle the mesh submeshes resolve against. Older
            // .demat files have none -> keep the (random) default; they're assigned by internal handle anyway.
            if ( m_Data.MaterialId )
                m_MaterialUUID = *m_Data.MaterialId;
            m_ReadyForUse = true;
            return BOOLSUCCESS;
        }

        // Backward compatibility: old cooker wrote Serialization::MaterialAssetData. Convert it.
        if ( const auto legacy = rfl::json::read<Serialization::MaterialAssetData>( raw ); legacy.has_value() )
        {
            m_Data         = FromLegacy( legacy.value() );
            m_MaterialUUID = legacy.value().MaterialHandle;
            m_ReadyForUse  = true;
            return BOOLSUCCESS;
        }

        // Neither format parsed — keep the editor usable with defaults; a re-save fixes the file.
        LOG_WARN( "[PBRMaterialAsset] '{}' could not be parsed in any known format; using defaults",
                  m_Metadata.Filepath.string() );
        m_Data        = PBRMaterialData{};
        m_ReadyForUse = true;
        return BOOLSUCCESS;
    }

    std::string PBRMaterialAsset::Save() const
    {
        return rfl::json::write( m_Data );
    }

    Common::BoolResultStr PBRMaterialAsset::Unload()
    {
        m_ReadyForUse = false;
        return BOOLSUCCESS;
    }

    bool PBRMaterialAsset::AddTexture( const Assets::AssetHandle& handle, TextureAsset::Type type,
                                       const glm::vec4& /*defaultColor*/ )
    {
        if ( auto* slot = HandleForType( type ) )
        {
            *slot = handle;
            return true;
        }
        return false;
    }

    std::optional<AssetHandle> PBRMaterialAsset::GetTextureHandle( TextureAsset::Type type ) const
    {
        const auto* slot = HandleForType( type );
        if ( !slot || static_cast<uint64_t>( *slot ) == 0 )
        {
            return std::nullopt;
        }
        return *slot;
    }

} // namespace Desert::Assets
