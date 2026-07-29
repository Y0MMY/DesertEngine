#include "SurfaceMaterialAsset.hpp"

#include <Engine/Assets/Mesh/PBRSurfaceParams.hpp>
#include <Engine/Core/Serialize/GLMReflect.hpp>
#include <Engine/Core/Serialize/CustomReflect.hpp>
#include <Engine/Assets/Serialization/Material.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <algorithm>
#include <rflcpp/rfl/json.hpp>

namespace Desert::Assets
{

    SurfaceMaterialAsset::SurfaceMaterialAsset( AssetPriority priority, const Common::Filepath& filepath )
         : MaterialAsset( priority, filepath, AssetTypeID::Material )
    {
    }

    const char* SurfaceMaterialAsset::SamplerNameForType( TextureAsset::Type type )
    {
        switch ( type )
        {
            case TextureAsset::Type::Albedo:    return "u_AlbedoTexture";
            case TextureAsset::Type::Normal:    return "u_NormalTexture";
            case TextureAsset::Type::Metallic:  return "u_MetallicTexture";
            case TextureAsset::Type::Roughness: return "u_RoughnessTexture";
            case TextureAsset::Type::AO:        return "u_AOTexture";
            case TextureAsset::Type::Emissive:  return "u_EmissiveTexture";
            default:                            return nullptr;
        }
    }

    std::optional<Assets::AssetHandle> SurfaceMaterialAsset::GetTextureHandle( TextureAsset::Type type ) const
    {
        const char* name = SamplerNameForType( type );
        if ( !name )
            return std::nullopt;
        const uint64_t h = m_Data.GetTexture( name );
        if ( h == 0 )
            return std::nullopt;
        return Assets::AssetHandle( h );
    }

    bool SurfaceMaterialAsset::AddTexture( const Assets::AssetHandle& handle, TextureAsset::Type type,
                                           const glm::vec4& /*defaultColor*/ )
    {
        const char* name = SamplerNameForType( type );
        if ( !name )
            return false;
        m_Data.SetTexture( name, static_cast<uint64_t>( handle ) );
        return true;
    }

    void SurfaceMaterialAsset::AdoptStableHandle()
    {
        // Asset-database identity: the internal handle must be STABLE across editor runs so
        // handle-based references (scene GUIDs, canon textures, service maps) survive restarts.
        // Priority: the GUID persisted INSIDE the file (also survives renames/moves), else a
        // path-derived handle (same as meshes/cooked textures).
        if ( m_Data.MaterialId )
            m_Metadata.Handle = *m_Data.MaterialId;
        else
            m_Metadata.Handle = Common::AssetHandle::FromCookedPath( m_Metadata.Filepath );
    }

    Common::BoolResultStr SurfaceMaterialAsset::Load()
    {
        const auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto finalize = [this]()
        {
            if ( m_Data.MaterialId )
                m_MaterialUUID = *m_Data.MaterialId;
            AdoptStableHandle();
            m_ReadyForUse = true;
        };

        if ( raw.empty() )
        {
            // New / empty material — canonical defaults; editable and re-savable.
            m_Data = MaterialData{};
            finalize();
            return BOOLSUCCESS;
        }

        // The unified MaterialData protocol is the ONLY on-disk format (pre-protocol migration
        // readers were removed with the rest of the legacy paths).
        if ( const auto parsed = rfl::json::read<MaterialData>( raw ); parsed.has_value() )
        {
            m_Data = parsed.value();
            finalize();
            return BOOLSUCCESS;
        }

        // Nothing parsed — keep the editor usable with defaults; a re-save fixes the file.
        // ERROR (not warn) on purpose: the authored parameters are LOST for this session and a
        // re-save makes that permanent — this must not scroll by silently.
        LOG_ERROR( "[SurfaceMaterialAsset] '{}' is corrupted/unparseable — rendering with DEFAULTS; "
                   "authored parameters are NOT applied and re-saving will overwrite the file.",
                   m_Metadata.Filepath.string() );
        m_Data = MaterialData{};
        finalize();
        return BOOLSUCCESS;
    }

    std::string SurfaceMaterialAsset::Save() const
    {
        return rfl::json::write( m_Data );
    }

    Common::BoolResultStr SurfaceMaterialAsset::Unload()
    {
        m_ReadyForUse = false;
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets
