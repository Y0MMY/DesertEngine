#pragma once

namespace Desert::Core::Formats
{
    enum class ImageUsage
    {
        Image2D,
        ImageCube,

        Attachment
    };

    enum class ImageFormat
    {
        RGBA8F,
        RGBA32F,
        BGRA8F,

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

    struct ImageSpecification
    {
        uint32_t        Width;
        uint32_t        Height;
        ImageFormat     Format;
        uint32_t        Mips = 1;
        ImagePixelData  Data;
        ImageUsage      Usage;
        ImageProperties Properties;
    };
} // namespace Desert::Core::Formats