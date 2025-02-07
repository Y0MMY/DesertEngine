#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Graphic
{
    class Image
    {
    public:
        virtual uint32_t                   GetWidth() const               = 0;
        virtual uint32_t                   GetHeight() const              = 0;
        virtual Core::Formats::ImageFormat GetImageFormat() const         = 0;
        virtual uint32_t                   GetMipmapLevels() const        = 0;
        virtual bool                       IsLoaded() const               = 0;
        virtual void                       Use( uint32_t slot = 0 ) const = 0;
    };

    class Image2D : public Image
    {
    public:
        static std::shared_ptr<Image2D> Create( const Core::Formats::ImageSpecification& spec );
    };

} // namespace Desert::Graphic
