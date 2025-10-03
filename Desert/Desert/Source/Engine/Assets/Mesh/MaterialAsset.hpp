#pragma once

#include <Engine/Assets/TextureAsset.hpp>

#include <Engine/Geometry/Mesh.hpp>

namespace Desert::Assets
{
    class MaterialAsset final : public AssetBase
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

        MaterialAsset( AssetPriority priority, const Common::Filepath& filepath );

        bool CopyFrom( const MaterialAsset& source );

        Common::BoolResultStr Load() override;
        Common::BoolResultStr Unload() override;

        bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        std::optional<std::reference_wrapper<const TextureSlot>> GetTextureSlot( TextureAsset::Type type ) const;
        std::shared_ptr<Graphic::Texture2D>                      GetTexture( TextureAsset::Type type ) const;

        bool AddTexture( const Common::Filepath& filepath, TextureAsset::Type type,
                         const glm::vec4& defaultColor = glm::vec4( 1.0f ) );

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

    private:
        bool m_ReadyForUse = false;

        std::array<std::unique_ptr<TextureSlot>, static_cast<size_t>( 6U )> m_TextureSlots;
    };
} // namespace Desert::Assets
