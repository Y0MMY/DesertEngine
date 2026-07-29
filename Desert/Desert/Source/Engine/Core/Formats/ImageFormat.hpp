#pragma once

#include <variant>
#include <optional>
#include <vector>
#include <string>
#include <cstdint>

namespace Desert::Core::Formats
{
    enum class Image2DUsage
    {
        Image2D,
        Attachment
    };

    enum class ImageFormat
    {
        RGBA8F,
        RGBA32F,
        BGRA8F,
        DEPTH24STENCIL8,

        DEPTH32F
    };

    enum ImageProperties : uint32_t
    {
        Storage = 0x1,
        Sample  = 0x2,
    };

    constexpr ImageProperties operator|( ImageProperties a, ImageProperties b )
    {
        return static_cast<ImageProperties>( static_cast<uint32_t>( a ) | static_cast<uint32_t>( b ) );
    }

    using ImagePixelData =
         std::variant<std::monostate, std::vector<float>, std::vector<unsigned char>, std::byte*>;
    using EmptyPixelData = std::monostate;

    inline bool HasData( const ImagePixelData& data )
    {
        return !std::holds_alternative<std::monostate>( data );
    }

    inline std::optional<std::vector<unsigned char>> GetUCharData( const ImagePixelData& data )
    {
        if ( const auto* vec = std::get_if<std::vector<unsigned char>>( &data ) )
        {
            return *vec;
        }
        return std::nullopt;
    }

    inline std::optional<std::vector<float>> GetFloatData( const ImagePixelData& data )
    {
        if ( const auto* vec = std::get_if<std::vector<float>>( &data ) )
        {
            return *vec;
        }
        return std::nullopt;
    }

    inline const std::byte* GetRawData( const ImagePixelData& data )
    {
        if ( const auto* ptr = std::get_if<std::byte*>( &data ) )
        {
            return *ptr;
        }
        return nullptr;
    }

    struct Image2DSpecification
    {
        const std::string     Tag;
        uint32_t              Width;
        uint32_t              Height;
        const ImageFormat     Format;
        uint32_t              Mips = 1;
        // MSAA sample count (1/2/4/8) — attachments only; a multisampled image must have Mips == 1
        // and is consumed by the render pass RESOLVE, not by ordinary samplers.
        uint32_t              Samples = 1;
        ImagePixelData        Data;
        const Image2DUsage    Usage;
        const ImageProperties Properties;
        // When true, the backend allocates a FULL mip chain (floor(log2(max(w,h)))+1) and generates the
        // lower mips from mip 0 via linear blits. Use for sampled textures that minify (e.g. foliage) so
        // they filter at distance instead of aliasing. Ignored if no pixel Data is supplied.
        bool                  GenerateMips = false;
    };

    struct ImageCubeSpecification
    {
        const std::string     Tag;
        const uint32_t        Width;
        const uint32_t        Height;
        const ImageFormat     Format;
        const uint32_t        Mips = 1;
        ImagePixelData        Data;
        const ImageProperties Properties;
    };
} // namespace Desert::Core::Formats