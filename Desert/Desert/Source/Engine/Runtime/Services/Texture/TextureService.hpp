#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Texture.hpp>

namespace Desert::Runtime
{
    class TextureService
    {
    public:
        void Register( const Assets::AssetHandle& handle, std::shared_ptr<Graphic::Texture2D> texture );
        std::shared_ptr<Graphic::Texture2D> Get( const Assets::AssetHandle& handle ) const;
        void                                Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Texture2D>> m_Textures;
    };
} // namespace Desert::Runtime