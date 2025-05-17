#pragma once
#pragma once

#include <Engine/Graphic/Image.hpp>
#include <filesystem>

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Graphic
{

    struct TextureSpecification
    {
        bool GenerateMips = true;
    };

    class Texture
    {
    public:
        virtual Common::BoolResult Invalidate() = 0;
    };

    class Texture2D : public Texture
    {
    public:
        Core::Formats::Image2DUsage GetType() const
        {
            return Core::Formats::Image2DUsage::Image2D;
        }
        virtual const std::shared_ptr<Image2D>& GetImage2D() const = 0;

        static std::shared_ptr<Texture2D> Create( const TextureSpecification&  specification,
                                                  const std::filesystem::path& path );
    };

    class TextureCube : public Texture
    {
    public:
        virtual const std::shared_ptr<ImageCube>& GetImageCube() const = 0;

        static std::shared_ptr<TextureCube> Create( const TextureSpecification&  specification,
                                                    const std::filesystem::path& path );
    };
} // namespace Desert::Graphic