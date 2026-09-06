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

    std::shared_ptr<SurfaceMaterialAsset>
    SurfaceMaterialAsset::CreateWorkingCopy( const SurfaceMaterialAsset& source )
    {
        auto copy =
             std::make_shared<SurfaceMaterialAsset>( source.m_Metadata.Priority, source.m_Metadata.Filepath );

        copy->m_Data = source.m_Data;
        // Carried over so a working copy of a material that is running on substituted defaults refuses to
        // save for the same reason its source does. Nothing saves the copy today, and this is what keeps
        // that true if something ever tries.
        copy->m_RunningOnSubstitutedDefaults = source.m_RunningOnSubstitutedDefaults;

        // Generated, and named as generated. A random id here is the one thing that keeps this copy out of
        // every map the subject is in — see the header for what a shared one would do to the mesh ->
        // material link. All three are written from ONE value because Load() maintains exactly that
        // equality (m_MaterialUUID = m_Metadata.Handle, adopted from Data().MaterialId), and a copy that
        // broke it would resolve differently depending on which of the three a caller happened to ask.
        const Common::UUID identity = Common::UUID::Generate();
        copy->m_Metadata.Handle     = identity;
        copy->m_MaterialUUID        = identity;
        copy->m_Data.MaterialId     = identity;

        // Never Load()ed, so nothing else would set this — and an asset that is not ready for use is
        // skipped by everything that would draw it.
        copy->m_ReadyForUse = true;
        return copy;
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
        // Asset-database identity: the internal handle must be STABLE across editor runs so handle-based
        // references (scene GUIDs, canon textures, service maps) survive restarts. A GUID persisted INSIDE
        // the file is the better identity because it survives renames and moves as well, so it wins when it
        // is there. When it is not, the path-derived handle AssetBase already installed stands — which is
        // why there is no `else` here.
        if ( m_Data.MaterialId )
            m_Metadata.Handle = *m_Data.MaterialId;
    }

    Common::BoolResultStr SurfaceMaterialAsset::Load()
    {
        const auto raw = Common::Utils::FileSystem::ReadFileContent( m_Metadata.Filepath );

        const auto finalize = [this]()
        {
            AdoptStableHandle();

            // The EXTERNAL id is the handle, always. When the file carries a MaterialId the two are the
            // same value by AdoptStableHandle; when it does not, they are the same path-derived value. The
            // external id used to be left unset in that second case, which under the old random default
            // meant MaterialService keyed such a material under a number that changed every launch — the
            // mesh->material link resolved through GetAssetHandleByExternal and missed after a restart.
            m_MaterialUUID = m_Metadata.Handle;

            m_ReadyForUse = true;
        };

        if ( !raw || raw.GetValue().empty() )
        {
            // New / empty material — canonical defaults; editable and re-savable. A MISSING file is
            // this branch by design too: the editor creates a material by naming a file that does
            // not exist yet (pinned by the AssetMissingFile suite).
            m_Data = MaterialData{};

            // BUT a file that IS there and could not be read is NOT a new material. Both used to land
            // here identically, with the reason ReadFileContent gave thrown away — so a .demat locked
            // by permissions, racing a delete or coming out of a truncated pak was presented to the
            // editor as a blank material somebody had just made, and the next save wrote that over it.
            // Exists() asks the VFS as well, so a packaged game answers this the same way.
            if ( !raw && Common::Utils::FileSystem::Exists( m_Metadata.Filepath ) )
            {
                LOG_ERROR( "[SurfaceMaterialAsset] '{}' EXISTS but could not be read ({}) — rendering "
                           "with DEFAULTS; the authored parameters are not applied and this material "
                           "will refuse to save over the file.",
                           m_Metadata.Filepath.string(), raw.GetError() );
                m_RunningOnSubstitutedDefaults = true;
            }

            finalize();
            return BOOLSUCCESS;
        }

        // The unified MaterialData protocol is the ONLY on-disk format (pre-protocol migration
        // readers were removed with the rest of the legacy paths).
        if ( const auto parsed = rfl::json::read<MaterialData>( raw.GetValue() ); parsed.has_value() )
        {
            m_Data                         = parsed.value();
            m_RunningOnSubstitutedDefaults = false; // a reload that parses clears a previous failure
            finalize();
            return BOOLSUCCESS;
        }

        // Nothing parsed — keep the editor usable with defaults.
        //
        // THIS BRANCH RETURNS SUCCESS DELIBERATELY, and the reason belongs here rather than in a
        // review comment: AssetManager::CreateAsset drops the asset entirely when Load answers an
        // error, so an unparseable .demat would vanish from the asset database, every mesh slot
        // pointing at it would resolve to nothing, and the user would be shown an empty material
        // picker instead of a material they can look at and fix. Refusing to load is a worse answer
        // than loading degraded — for the FILE, though, not for the DATA: the half that was actually
        // destructive is the re-save this message used to warn about while nothing could stop it, and
        // that is now refused by Save() below. Whether an unloadable asset should additionally mark
        // the whole SCENE as degraded is a larger change to the load path (audit Д31-8) and is not
        // decided here.
        LOG_ERROR( "[SurfaceMaterialAsset] '{}' is corrupted/unparseable — rendering with DEFAULTS; "
                   "authored parameters are NOT applied and this material will refuse to save over "
                   "the file.",
                   m_Metadata.Filepath.string() );
        m_Data                         = MaterialData{};
        m_RunningOnSubstitutedDefaults = true;
        finalize();
        return BOOLSUCCESS;
    }

    Common::ResultStr<std::string> SurfaceMaterialAsset::Save() const
    {
        if ( m_RunningOnSubstitutedDefaults )
            return Common::MakeFormattedError<std::string>(
                 "'{}' is running on substituted defaults because its file could not be read or parsed; "
                 "writing them out would destroy the authored parameters permanently. Fix or delete the "
                 "file first.",
                 m_Metadata.Filepath.string() );

        return Common::MakeSuccess( rfl::json::write( m_Data ) );
    }

    Common::BoolResultStr SurfaceMaterialAsset::Unload()
    {
        m_ReadyForUse = false;
        return BOOLSUCCESS;
    }
} // namespace Desert::Assets
