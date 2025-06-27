#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

namespace Desert::Graphic
{
    class FallbackTextures : public DynamicResources
    {
    public:
        virtual const std::shared_ptr<Image2D>&
        GetFallbackTexture2D( Core::Formats::ImageFormat format ) const = 0;
        virtual const std::shared_ptr<ImageCube>&
        GetFallbackTextureCube( Core::Formats::ImageFormat format ) const = 0;

    public:
        FallbackTextures( const FallbackTextures& )            = delete;
        FallbackTextures& operator=( const FallbackTextures& ) = delete;

        static FallbackTextures& Get();

    protected:
        FallbackTextures() = default;
    };
} // namespace Desert::Graphic