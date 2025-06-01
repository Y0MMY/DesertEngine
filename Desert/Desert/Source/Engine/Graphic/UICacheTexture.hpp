#pragma once

#include <Engine/Graphic/Image.hpp>

namespace Desert::Graphic
{
    class UICacheTexture
    {
    public:
        virtual ~UICacheTexture() = default;

        virtual const void* AddTextureCache( const std::shared_ptr<Image2D>& image ) = 0;

        static std::shared_ptr<UICacheTexture> Create();
    };
} // namespace Desert::Graphic