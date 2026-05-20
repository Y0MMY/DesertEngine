#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Assets
{
    class PBRMaterialAsset final : public MaterialAsset
    {
    public:
        struct TextureSlot
        {
            AssetHandle TextureHandle;
            glm::vec4   DefaultColor = glm::vec4( 1.0f );
            bool        IsValid() const
            {
                return TextureHandle != 0;
            }
        };

        PBRMaterialAsset( AssetPriority priority, const Common::Filepath& filepath );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

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

        std::optional<glm::vec3> GetAlbedoColor() const
        {
            return m_Parameters.AlbedoColor;
        }

        std::optional<glm::vec3> GetEmissiveColor() const
        {
            return m_Parameters.EmissiveColor;
        }

        std::optional<float> GetMetallicFactor() const
        {
            return m_Parameters.MetallicFactor;
        }

        std::optional<float> GetRoughnessFactor() const
        {
            return m_Parameters.RoughnessFactor;
        }

        std::optional<float> GetAOStrength() const
        {
            return m_Parameters.AOStrength;
        }

        std::optional<float> GetEmissiveIntensity() const
        {
            return m_Parameters.EmissiveIntensity;
        }

    private:
        bool m_ReadyForUse = false;

        std::array<TextureSlot, static_cast<size_t>( 6U )> m_TextureSlots;

        Common::UUID m_MaterialUUID;

        struct PBRParameters
        {
            glm::vec3 AlbedoColor       = glm::vec3( 1.0f );
            glm::vec3 EmissiveColor     = glm::vec3( 0.0f );
            float     MetallicFactor    = 0.0f;
            float     RoughnessFactor   = 0.5f;
            float     AOStrength        = 1.0f;
            float     EmissiveIntensity = 1.0f;

            // Flags to track which parameters are explicitly set
            bool bHasAlbedoColor       = false;
            bool bHasMetallicFactor    = false;
            bool bHasRoughnessFactor   = false;
            bool bHasEmissiveColor     = false;
            bool bHasEmissiveIntensity = false;
            bool bHasAOStrength        = false;
        } m_Parameters;
    };
} // namespace Desert::Assets
