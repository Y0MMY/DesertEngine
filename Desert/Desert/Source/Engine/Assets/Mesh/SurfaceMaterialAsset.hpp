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

        // Serialize the canonical data to .demat JSON.
        std::string Save() const;

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

        // Replaces the ctor's random handle with a stable one (MaterialId from the file, else
        // path-derived) — asset-database identity that survives restarts and renames.
        void AdoptStableHandle();

        bool         m_ReadyForUse = false;
        Common::UUID m_MaterialUUID;
        MaterialData m_Data;
    };
} // namespace Desert::Assets
