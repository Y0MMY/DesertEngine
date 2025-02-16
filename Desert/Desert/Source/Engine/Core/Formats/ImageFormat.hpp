#pragma once

namespace Desert::Core::Formats
{
    enum class ImageType
    {
        Image2D,
        ImageCube
    };

    enum class ImageFormat
    {
        RGBA8F,
    };

    struct ImageSpecification
    {
        uint32_t                  Width;
        uint32_t                  Height;
        ImageFormat               Format;
        uint32_t                  TextureSamples = 1;
        std::optional<std::byte*> Data;
        ImageType                 Type;
    };
} // namespace Desert::Core::Formats