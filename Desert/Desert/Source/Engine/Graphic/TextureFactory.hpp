#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Texture.hpp>

namespace Desert::Graphic
{
    class TextureFactory
    {
    public:
        static std::shared_ptr<Texture2D> Create2D( const std::shared_ptr<Assets::TextureAsset>& asset )
        {
            if ( !asset )
            {
                return nullptr;
            }

            const auto textureResult = Texture2D::Create( {}, asset->GetSourcePath() );
            if ( !textureResult.IsSuccess() )
            {
                return nullptr;
            }

            return textureResult.GetValue();
        }
    };
} // namespace Desert::Graphic