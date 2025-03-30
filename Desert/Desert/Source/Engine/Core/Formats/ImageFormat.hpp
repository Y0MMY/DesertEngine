#pragma once

namespace Desert::Core::Formats
{
    enum class ImageUsage
    {
        Image2D,
        ImageCube,
        Storage,

        Attachment
    };

    enum class ImageFormat
    {
        RGBA8F,
        BGRA8F,

        DEPTH32F
    };

    /* enum ImageProperties
     {
         Store,
     };*/

    struct ImageSpecification
    {
        uint32_t                  Width;
        uint32_t                  Height;
        ImageFormat               Format;
        uint32_t                  TextureSamples = 1;
        std::optional<std::byte*> Data;
        ImageUsage                Usage;
        // ImageProperties           Properties;
    };
} // namespace Desert::Core::Formats