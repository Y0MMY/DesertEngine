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
        virtual ~Texture()                      = default;
        virtual Common::BoolResultStr Invalidate() = 0;
        virtual void Release() = 0;

        virtual uint32_t GetWidth() const  = 0;
        virtual uint32_t GetHeight() const = 0;
    };

    class Texture2D : public Texture
    {
    public:
        virtual ~Texture2D() = default;

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
        virtual ~TextureCube() = default;

        virtual const std::shared_ptr<ImageCube>& GetImageCube() const = 0;

        static std::shared_ptr<TextureCube> Create( const TextureSpecification&  specification,
                                                    const std::filesystem::path& path );
    };
} // namespace Desert::Graphic