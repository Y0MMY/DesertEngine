#pragma once

#include <Engine/Graphic/Image.hpp>
#include <filesystem>

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Graphic
{

    class Texture
    {
    public:
        virtual Core::Formats::ImageUsage GetType() const    = 0;
        virtual Common::BoolResult        Invalidate()  = 0;
    };

    class Texture2D : public Texture
    {
    public:
        virtual Core::Formats::ImageUsage GetType() const override
        {
            return Core::Formats::ImageUsage::Image2D;
        }
        virtual const std::shared_ptr<Image2D>& GetImage2D() const = 0;

        static std::shared_ptr<Texture2D> Create( const std::filesystem::path& path );
    };

    class TextureCube : public Texture
    {
    public:
        virtual Core::Formats::ImageUsage GetType() const override
        {
            return Core::Formats::ImageUsage::Image2D;
        }
        virtual const std::shared_ptr<Image2D>& GetImage2D() const = 0;

        static std::shared_ptr<TextureCube> Create( const std::filesystem::path& path );
    };
} // namespace Desert::Graphic