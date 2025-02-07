#pragma once

#include <Engine/Graphic/Image.hpp>
#include <filesystem>

namespace Desert::Graphic
{
    enum class TextureType
    {
        Texture2D,
        Texture3D
    };

    class Texture
    {
    public:
        virtual TextureType GetType() const = 0;
    };

    class Texture2D : public Texture
    {
    public:
        virtual TextureType GetType() const override
        {
            return TextureType::Texture2D;
        }
        virtual const std::shared_ptr<Image2D>& GetImage2D() const = 0;

        static std::shared_ptr<Texture2D> Create( const std::filesystem::path& path );
    };
} // namespace Desert::Graphic