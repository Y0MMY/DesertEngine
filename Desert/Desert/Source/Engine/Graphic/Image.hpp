#pragma once

#include <Engine/Core/Formats/ImageFormat.hpp>
#include <Engine/Graphic/DynamicResources.hpp>

#include <Engine/Graphic/MipMapGenerator.hpp>

#include <Common/Core/UUID.hpp>

namespace Desert::Graphic
{
    class Image3D;

    using ImageCubeRef = std::shared_ptr<ImageCube>;
    using Image2DRef   = std::shared_ptr<Image2D>;
    using Image3DRef   = std::shared_ptr<Image3D>;

    class Image
    {
    public:
        Image() : m_Hash( Common::UUID() )
        {
        }

        virtual ~Image() = default;

        virtual uint32_t                      GetWidth() const               = 0;
        virtual uint32_t                      GetHeight() const              = 0;
        virtual Core::Formats::ImageFormat    GetImageFormat() const         = 0;
        virtual uint32_t                      GetMipmapLevels() const        = 0;
        virtual bool                          IsLoaded() const               = 0;
        virtual void                          Use( uint32_t slot = 0 ) const = 0;
        virtual Core::Formats::ImagePixelData GetImagePixels()               = 0;

        virtual const Common::UUID GetHash() const final
        {
            return m_Hash;
        }

        // Byte size and bytes-per-pixel now live with the format they describe, as total functions:
        // Core::Formats::GetBytesPerPixel / CalculateImageSize (Core/Formats/ImageFormat.hpp). They used
        // to be statics here that returned 0 for any format they did not recognise.

    private:
        const Common::UUID m_Hash;
    };

    class Image2D : public Image, public DynamicResources
    {
    public:
        virtual ~Image2D() = default;

        virtual Core::Formats::Image2DSpecification& GetImageSpecification() = 0;

        // Reads the image back to CPU as tightly-packed RGBA8 (size = width*height*4). Returns empty on
        // failure / unsupported format. Used for offscreen thumbnail capture (render -> readback -> PNG).
        virtual std::vector<uint8_t> ReadPixelsRGBA8() { return {}; }

        // Re-uploads tightly-packed pixel data into the EXISTING GPU image without recreating it — the
        // image (and any descriptor sets bound to its pointer) stays valid, so this is the safe, churn-free
        // way to stream changing content (video frames) every frame. `data` must match the image's format
        // and dimensions. Default: unsupported.
        virtual Common::BoolResultStr SetData( const Core::Formats::ImagePixelData& data )
        {
            return Common::MakeError<bool>( "Image2D::SetData not supported by this backend" );
        }

        static std::shared_ptr<Image2D> Create( const Core::Formats::Image2DSpecification& spec,
                                                const std::unique_ptr<MipMap2DGenerator>&  mipGenerator );
    };

    class ImageCube : public Image, public DynamicResources
    {
    public:
        virtual ~ImageCube() = default;

        virtual Core::Formats::ImageCubeSpecification& GetImageSpecification() = 0;

        static std::shared_ptr<ImageCube> Create( const Core::Formats::ImageCubeSpecification& spec,
                                                  const std::unique_ptr<MipMapCubeGenerator>&  mipGenerator );
        static std::shared_ptr<ImageCube> Copy( const std::shared_ptr<ImageCube>& targetImageCube );
    };

    /**
     * @brief A volume texture: sampled as `sampler3D`, written by compute as `image3D`.
     *
     * Single mip level by design — see the note on Core::Formats::Image3DSpecification. Create() takes no
     * mip generator for the same reason: an argument that could only ever be ignored.
     *
     * The backend gives every volume a LINEAR / REPEAT sampler that does NOT follow the global Scene
     * Settings texture filter. Interpolating a noise volume is part of the algorithm, not a quality
     * preference: with "Nearest" selected for textures, a trilinearly-sampled cloud volume would turn
     * into visible voxels.
     */
    class Image3D : public Image, public DynamicResources
    {
    public:
        virtual ~Image3D() = default;

        [[nodiscard]] virtual uint32_t GetDepth() const = 0;

        virtual Core::Formats::Image3DSpecification& GetImageSpecification() = 0;

        static std::shared_ptr<Image3D> Create( const Core::Formats::Image3DSpecification& spec );
    };

    namespace Utils
    {
        bool                   IsDepthFormat( Core::Formats::ImageFormat format );
        bool                   HasStencilComponent( Core::Formats::ImageFormat format );
        inline uint32_t        CalculateMipCount( uint32_t width, uint32_t height, uint32_t depth = 1 )
        {
            // Parenthesize to defeat any windows.h max() macro that may be active in the including TU.
            uint32_t max_dim = ( std::max )( { width, height, depth } );
            return max_dim > 0 ? (uint32_t)std::log2( max_dim ) + 1 : 1;
        }
    } // namespace Utils

} // namespace Desert::Graphic
