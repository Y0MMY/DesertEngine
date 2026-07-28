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
    namespace
    {
        // ── Migration-only readers (never written; Save() emits MaterialData only) ──────────

        // Pre-protocol .demat layout: typed PBR fields at top level (+ the transitional optional
        // canon of v3/v4 builds). Exists solely so old files parse; converted to MaterialData
        // right after the read and the file is upgraded on disk.
        struct LegacyTypedMaterial
        {
            glm::vec4 AlbedoColor       = glm::vec4( 1.0f );
            float     MetallicFactor    = 0.0f;
            float     RoughnessFactor   = 0.5f;
            float     AOStrength        = 1.0f;
            glm::vec4 EmissiveColor     = glm::vec4( 0.0f, 0.0f, 0.0f, 1.0f );
            float     EmissiveIntensity = 1.0f;
            float     AlphaCutoff       = 0.0f;
            float     Transmission      = 0.0f;
            float     IOR               = 1.5f;
            glm::vec4 GlassTint         = glm::vec4( 1.0f );

            AssetHandle AlbedoTexture{ 0ULL };
            AssetHandle NormalTexture{ 0ULL };
            AssetHandle MetallicTexture{ 0ULL };
            AssetHandle RoughnessTexture{ 0ULL };
            AssetHandle AOTexture{ 0ULL };
            AssetHandle EmissiveTexture{ 0ULL };
            AssetHandle OpacityTexture{ 0ULL };

            std::optional<Common::UUID> MaterialId;
            std::optional<glm::vec2>    UVTiling;

            // Transitional canon written by v3/v4 builds.
            std::optional<std::string>                        ShaderName;
            std::optional<std::vector<MaterialShaderParam>>   ShaderParams;
            std::optional<std::vector<MaterialShaderTexture>> ShaderTextures;
        };

        MaterialData FromLegacyTyped( const LegacyTypedMaterial& legacy )
        {
            // Transitional files already carry the canon — take it verbatim.
            if ( legacy.ShaderParams || legacy.ShaderTextures )
            {
                MaterialData m;
                m.ShaderName = legacy.ShaderName;
                if ( legacy.ShaderParams )
                    m.Params = *legacy.ShaderParams;
                if ( legacy.ShaderTextures )
                    m.Textures = *legacy.ShaderTextures;
                m.MaterialId = legacy.MaterialId;
                return m;
            }

            // Pure typed layout -> build the canon through the typed view.
            PBRSurfaceParams p;
            p.AlbedoColor       = legacy.AlbedoColor;
            p.MetallicFactor    = legacy.MetallicFactor;
            p.RoughnessFactor   = legacy.RoughnessFactor;
            p.AOStrength        = legacy.AOStrength;
            p.EmissiveColor     = legacy.EmissiveColor;
            p.EmissiveIntensity = legacy.EmissiveIntensity;
            p.AlphaCutoff       = legacy.AlphaCutoff;
            p.Transmission      = legacy.Transmission;
            p.IOR               = legacy.IOR;
            p.GlassTint         = legacy.GlassTint;
            p.AlbedoTexture     = legacy.AlbedoTexture;
            p.NormalTexture     = legacy.NormalTexture;
            p.MetallicTexture   = legacy.MetallicTexture;
            p.RoughnessTexture  = legacy.RoughnessTexture;
            p.AOTexture         = legacy.AOTexture;
            p.EmissiveTexture   = legacy.EmissiveTexture;
            p.OpacityTexture    = legacy.OpacityTexture;
            p.UVTiling          = legacy.UVTiling;
            p.MaterialId        = legacy.MaterialId;
            return p.ToMaterialData();
        }

        // Ancient cooker format (.mat / Serialization::MaterialAssetData).
        MaterialData FromAncient( const Serialization::MaterialAssetData& legacy )
        {
            PBRSurfaceParams p;
            p.AlbedoColor     = legacy.Albedo.Value;
            p.MetallicFactor  = legacy.Metallic.Value;
            p.RoughnessFactor = legacy.Roughness.Value;

            // The old cooker sometimes wrote garbage AO — keep it only if sane, else default to 1.
            const float ao = legacy.AO.Value;
            p.AOStrength    = ( ao >= 0.0f && ao <= 1.0f ) ? ao : 1.0f;
            p.EmissiveColor = glm::vec4( legacy.Emissive.Value, 1.0f );

            if ( legacy.Albedo.Texture )
                p.AlbedoTexture = legacy.Albedo.Texture->Handle;
            if ( legacy.Metallic.Texture )
                p.MetallicTexture = legacy.Metallic.Texture->Handle;
            if ( legacy.Roughness.Texture )
                p.RoughnessTexture = legacy.Roughness.Texture->Handle;
            if ( legacy.AO.Texture )
                p.AOTexture = legacy.AO.Texture->Handle;
            if ( legacy.Emissive.Texture )
                p.EmissiveTexture = legacy.Emissive.Texture->Handle;
            // The ancient format has no normal-map slot.
            return p.ToMaterialData();
        }
    } // namespace

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

        // Canonical format. (Params is a required field, so pre-protocol files fail this parse
        // and fall through to the migration readers below.)
        if ( const auto parsed = rfl::json::read<MaterialData>( raw ); parsed.has_value() )
        {
            m_Data = parsed.value();
            finalize();
            return BOOLSUCCESS;
        }

        // Pre-protocol typed layout (incl. the transitional v3/v4 canon) — migrate + upgrade the
        // file on disk so the legacy format disappears from the project on first touch.
        if ( const auto legacy = rfl::json::read<LegacyTypedMaterial>( raw ); legacy.has_value() )
        {
            m_Data = FromLegacyTyped( legacy.value() );
            finalize();
            Common::Utils::FileSystem::WriteContentToFile( m_Metadata.Filepath, Save() );
            LOG_INFO( "[Material] '{}' migrated to the unified protocol", m_Metadata.Filepath.string() );
            return BOOLSUCCESS;
        }

        // Ancient cooker format.
        if ( const auto legacy = rfl::json::read<Serialization::MaterialAssetData>( raw ); legacy.has_value() )
        {
            m_Data         = FromAncient( legacy.value() );
            m_MaterialUUID = legacy.value().MaterialHandle;
            if ( !m_Data.MaterialId )
                m_Data.MaterialId = m_MaterialUUID;
            finalize();
            Common::Utils::FileSystem::WriteContentToFile( m_Metadata.Filepath, Save() );
            LOG_INFO( "[Material] '{}' migrated to the unified protocol", m_Metadata.Filepath.string() );
            return BOOLSUCCESS;
        }

        // Nothing parsed — keep the editor usable with defaults; a re-save fixes the file.
        LOG_WARN( "[SurfaceMaterialAsset] '{}' could not be parsed in any known format; using defaults",
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
