#pragma once

namespace Desert::Core::Formats
{
    enum class ImageFormat
    {
        RGB,
        RGBA,
    };

    struct ImageSpecification
    {
        uint32_t                  Width;
        uint32_t                  Height;
        ImageFormat               Format;
        uint32_t                  TextureSamples = 1;
        std::optional<std::byte*> Data;
    };
} // namespace Desert::Core::Formats