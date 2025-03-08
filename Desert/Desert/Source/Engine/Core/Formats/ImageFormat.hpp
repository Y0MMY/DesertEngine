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

        DEPTH32F
    };

    struct ImageSpecification
    {
        uint32_t                  Width;
        uint32_t                  Height;
        ImageFormat               Format;
        uint32_t                  TextureSamples = 1;
        std::optional<std::byte*> Data;
        ImageUsage                Usage;
    };
} // namespace Desert::Core::Formats