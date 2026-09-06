#pragma once

#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/MaterialData.hpp>
#include <Engine/Assets/TextureAsset.hpp>

namespace Desert::Assets
{
    // The concrete material asset (.demat): a shader + generic parameter values (MaterialData —
    // THE single material protocol; the shader's schema defines what the params mean).
    // "StaticMeshPBR" (the default) routes to the optimized batched backend; any Surface-domain
    // DSL shader routes to the generic per-object path. Pre-protocol files (typed PBR fields /
    // the ancient cooker format) are migrated on Load and the file is upgraded on disk.
    class SurfaceMaterialAsset final : public MaterialAsset
    {
    public:
        SurfaceMaterialAsset( AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        // Serialize the canonical data to .demat JSON — or REFUSE, when this asset is running on
        // substituted defaults because its file could not be read or parsed (see Load). Writing that
        // out is what turns a recoverable corruption into a permanent loss of the authored parameters,
        // and it is the exact thing Load's own error message used to warn about while returning
        // success and leaving nothing able to stop it. A Result rather than a flag beside the getter
        // because a flag has to be REMEMBERED at three call sites and this cannot be forgotten.
        [[nodiscard]] Common::ResultStr<std::string> Save() const;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        // Canonical data — single source of truth for the editor UI, serialization and the
        // runtime material build.
        MaterialData&       Data()       { return m_Data; }
        const MaterialData& Data() const { return m_Data; }

        // Texture-slot convenience for the importer/editor (maps the slot enum to the shader's
        // sampler name in the canon).
        std::optional<Assets::AssetHandle> GetTextureHandle( TextureAsset::Type type ) const;
        bool AddTexture( const Assets::AssetHandle& handle, TextureAsset::Type type,
                         const glm::vec4& defaultColor = glm::vec4( 1.0f ) );

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

        virtual std::string GetShaderName() const override
        {
            return m_Data.EffectiveShaderName();
        }

        virtual Common::UUID GetMaterialUUID() const override
        {
            return m_MaterialUUID;
        }

    private:
        // Maps a texture-slot enum to the shader schema's sampler name.
        static const char* SamplerNameForType( TextureAsset::Type type );

        // Upgrades the path-derived handle AssetBase installed to the in-file MaterialId when the file has
        // one — asset-database identity that survives renames as well as restarts.
        void AdoptStableHandle();

        bool         m_ReadyForUse  = false;
        Common::UUID m_MaterialUUID = Common::UUID::Null();
        MaterialData m_Data;

        // TRUE when m_Data is NOT what the file says — the file exists but could not be read, or it
        // read and would not parse. The asset is deliberately still usable in that state (see Load),
        // so this is the only thing that distinguishes "a material with default values" from "a
        // material whose values were lost this session", and Save() is what asks.
        bool m_RunningOnSubstitutedDefaults = false;
    };
} // namespace Desert::Assets
