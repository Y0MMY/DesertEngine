#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

namespace Desert::Graphic
{
    class Image : public DynamicResources
    {
    public:
        virtual ~Image() = default;

        virtual uint32_t                      GetWidth() const               = 0;
        virtual uint32_t                      GetHeight() const              = 0;
        virtual Core::Formats::ImageFormat    GetImageFormat() const         = 0;
        virtual uint32_t                      GetMipmapLevels() const        = 0;
        virtual bool                          IsLoaded() const               = 0;
        virtual void                          Use( uint32_t slot = 0 ) const = 0;
        virtual Core::Formats::ImagePixelData GetImagePixels() const         = 0;

        virtual const std::string GetHash() const = 0;

        static uint32_t GetBytesPerPixel( const Core::Formats::ImageFormat& format );
        // Calculates the byte size of an image based on dimensions and format
        static uint32_t CalculateImageSize( uint32_t width, uint32_t height,
                                            const Core::Formats::ImageFormat& format );
    };

    class Image2D : public Image
    {
    public:
        virtual Core::Formats::Image2DSpecification& GetImageSpecification() = 0;

        static std::shared_ptr<Image2D> Create( const Core::Formats::Image2DSpecification& spec );
    };

    class ImageCube : public Image
    {
    public:
        virtual Core::Formats::ImageCubeSpecification& GetImageSpecification() = 0;

        static std::shared_ptr<ImageCube> Create( const Core::Formats::ImageCubeSpecification& spec );
    };

    namespace Utils
    {
        static inline uint32_t GetBytesPerPixel( const Core::Formats::ImageFormat& format );
        bool                   IsDepthFormat( Core::Formats::ImageFormat format );
        inline uint32_t        CalculateMipCount( uint32_t width, uint32_t height, uint32_t depth = 1 )
        {
            uint32_t max_dim = std::max( { width, height, depth } );
            return max_dim > 0 ? (uint32_t)std::log2( max_dim ) + 1 : 1;
        }
    } // namespace Utils

} // namespace Desert::Graphic
