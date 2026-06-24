#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Mesh/PBRMaterialData.hpp>

#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Assets
{
    // PBR material asset backed entirely by the reflected PBRMaterialData (see PBRMaterialData.hpp).
    // Load/Save are automated via reflect-cpp — there is no per-parameter (de)serialization code.
    class PBRMaterialAsset final : public MaterialAsset
    {
    public:
        PBRMaterialAsset( AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        // Serialize the reflected data back to a .lmat (JSON via reflect-cpp).
        std::string Save() const;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        // Canonical reflected data — single source of truth for editor UI, serialization and shader upload.
        PBRMaterialData&       Data()       { return m_Data; }
        const PBRMaterialData& Data() const { return m_Data; }

        std::optional<Assets::AssetHandle> GetTextureHandle( TextureAsset::Type type ) const;

        bool AddTexture( const Assets::AssetHandle& handle, TextureAsset::Type type,
                         const glm::vec4& defaultColor = glm::vec4( 1.0f ) );

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

        virtual MaterialType GetMaterialType() const override
        {
            return MaterialAsset::MaterialType::PBR;
        }

        virtual Common::UUID GetMaterialUUID() const override
        {
            return m_MaterialUUID;
        }

        // Convenience accessors (forward to the reflected data) kept for existing consumers such as
        // MaterialFactory. New code should prefer Data().
        std::optional<glm::vec3> GetAlbedoColor() const    { return glm::vec3( m_Data.AlbedoColor ); }
        std::optional<glm::vec3> GetEmissiveColor() const  { return glm::vec3( m_Data.EmissiveColor ); }
        std::optional<float>     GetMetallicFactor() const { return m_Data.MetallicFactor; }
        std::optional<float>     GetRoughnessFactor() const{ return m_Data.RoughnessFactor; }
        std::optional<float>     GetAOStrength() const     { return m_Data.AOStrength; }
        std::optional<float>     GetEmissiveIntensity() const { return m_Data.EmissiveIntensity; }

    private:
        // Maps a texture-slot enum to the matching handle field in the reflected data.
        const Assets::AssetHandle* HandleForType( TextureAsset::Type type ) const;
        Assets::AssetHandle*       HandleForType( TextureAsset::Type type );

        bool            m_ReadyForUse = false;
        Common::UUID    m_MaterialUUID;
        PBRMaterialData m_Data;
    };
} // namespace Desert::Assets
