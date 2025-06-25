#pragma once

#include <Engine/Assets/TextureAsset.hpp>

#include <Engine/Graphic/Mesh.hpp>

namespace Desert::Assets
{
    class MaterialAsset final : public AssetBase
    {
    public:
        struct TextureSlot
        {
            std::unique_ptr<TextureAsset> Texture;
            glm::vec4                     DefaultColor;
        };
        MaterialAsset( const AssetPriority priority, const Common::Filepath& filepath );

        virtual Common::BoolResult Load() override;
        virtual Common::BoolResult Unload() override;

        virtual bool IsReadyForUse() const
        {
            return m_ReadyForUse;
        }

        bool HasTexture( TextureAsset::Type type ) const
        {
            return m_TextureLookup.find( type ) != m_TextureLookup.end();
        }

        static AssetTypeID GetTypeID()
        {
            return AssetTypeID::Material;
        }

        const TextureSlot* GetTextureSlot( TextureAsset::Type type ) const
        {
            auto it = m_TextureLookup.find( type );
            if ( it != m_TextureLookup.end() )
                return it->second->get();
            return nullptr;
        }

        const std::shared_ptr<Graphic::Texture2D>& GetTexture( TextureAsset::Type type ) const
        {
            auto it = m_TextureLookup.find( type );
            if ( it != m_TextureLookup.end() && ( *it->second )->Texture &&
                 ( *it->second )->Texture->IsReadyForUse() )
            {
                return ( *it->second )->Texture->GetTexture();
            }
            return nullptr;
        }

        const auto& GetTextureLookup() const
        {
            return m_TextureLookup;
        }

        static AssetManager::KeyHandle GetAssetKey( const Common::Filepath& filepath );

        const auto& GetMaterialAssetPath() const
        {
            return m_MaterialAssetPath;
        }

    private:
        const Common::Filepath m_MaterialAssetPath;

        bool                                      m_ReadyForUse = false;
        std::vector<std::unique_ptr<TextureSlot>> m_TextureSlots;
        std::unordered_map<TextureAsset::Type, typename std::vector<std::unique_ptr<TextureSlot>>::iterator>
             m_TextureLookup;
    };

} // namespace Desert::Assets