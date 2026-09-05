#pragma once

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Graphic/Texture.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Graphic
{
    class TextureFactory
    {
    public:
        // Pixels are loaded from GetSourcePath() — the path TextureAsset::Load resolved from the .tex's
        // root-tagged key. A failure is LOGGED with the path and the reason before nullptr comes back:
        // this used to fail in silence, and a texture that quietly is not there is the most expensive
        // kind of missing.
        static std::shared_ptr<Texture2D> Create2D( const std::shared_ptr<Assets::TextureAsset>& asset )
        {
            if ( !asset )
            {
                LOG_ERROR( "[TextureFactory] Create2D was handed a null TextureAsset; no texture is built." );
                return nullptr;
            }

            const auto textureResult = Texture2D::Create( {}, asset->GetSourcePath() );
            if ( !textureResult.IsSuccess() )
            {
                LOG_ERROR( "[TextureFactory] Building the GPU texture for '{0}' (asset '{1}') failed: {2}",
                           asset->GetSourcePath(), asset->GetMetadata().Filepath.string(),
                           textureResult.GetError() );
                return nullptr;
            }

            return textureResult.GetValue();
        }
    };
} // namespace Desert::Graphic