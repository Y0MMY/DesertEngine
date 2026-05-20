#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Texture.hpp>

namespace Desert::Runtime
{
    class TextureService
    {
    public:
        void Register( const std::shared_ptr<Assets::TextureAsset>& texture );
        Graphic::Texture2D*
             Get( const Assets::AssetHandle& handle ) const; // TODO: RETURN RUNTIME TUEXTURE'S HANDLE
        void Clear();

    private:
        std::unordered_map<Assets::AssetHandle, std::shared_ptr<Graphic::Texture2D>> m_Textures;
    };
} // namespace Desert::Runtime