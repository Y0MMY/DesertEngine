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
            std::unique_ptr<TextureAsset> Texture;
            glm::vec4                     DefaultColor = glm::vec4( 1.0f );
            bool                          IsValid() const
            {
                return Texture != nullptr;
            }
        };

        PBRMaterialAsset( AssetPriority priority, const Common::Filepath& filepath );

        bool CopyFrom( const MaterialAsset& source );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        std::optional<std::reference_wrapper<const TextureSlot>> GetTextureSlot( TextureAsset::Type type ) const;
        TextureAsset*                                            GetTexture( TextureAsset::Type type ) const;

        bool AddTexture( const Common::Filepath& filepath, TextureAsset::Type type,
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

    private:
        bool m_ReadyForUse = false;

        std::array<std::unique_ptr<TextureSlot>, static_cast<size_t>( 6U )> m_TextureSlots;

        Common::UUID m_MaterialUUID;
    };
} // namespace Desert::Assets
