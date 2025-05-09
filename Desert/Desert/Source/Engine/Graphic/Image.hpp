#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>

namespace Desert::Graphic
{
    class Image
    {
    public:
        virtual uint32_t                           GetWidth() const               = 0;
        virtual uint32_t                           GetHeight() const              = 0;
        virtual Core::Formats::ImageFormat         GetImageFormat() const         = 0;
        virtual uint32_t                           GetMipmapLevels() const        = 0;
        virtual bool                               IsLoaded() const               = 0;
        virtual void                               Use( uint32_t slot = 0 ) const = 0;
        virtual Core::Formats::ImageSpecification& GetImageSpecification()        = 0;
        virtual Core::Formats::ImagePixelData      GetImagePixels() const         = 0;

        static uint32_t GetBytesPerPixel( const Core::Formats::ImageFormat& format );
        // Calculates the byte size of an image based on dimensions and format
        static uint32_t CalculateImageSize( uint32_t width, uint32_t height,
                                            const Core::Formats::ImageFormat& format );
    };

    class Image2D : public Image
    {
    public:
        static std::shared_ptr<Image2D> Create( const Core::Formats::ImageSpecification& spec );
    };

    namespace Utils
    {
        static inline uint32_t GetBytesPerPixel( const Core::Formats::ImageFormat& format );
        bool                   IsDepthFormat( Core::Formats::ImageFormat format );
        inline uint32_t        CalculateMipCount( uint32_t width, uint32_t height )
        {
            return (uint32_t)std::floor( std::log2( std::min( width, height ) ) ) + 1;
        }
    } // namespace Utils

} // namespace Desert::Graphic
