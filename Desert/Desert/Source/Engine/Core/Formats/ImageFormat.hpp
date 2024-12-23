#pragma once

namespace Desert::Core::Formats
{
    enum class ImageFormat
    {
        None = 0,
        RGB,
        RGBA,
        RGBA16F,
        RGBA32F,

        DEPTH32F,

        DEPTH24STENCIL8
    };
}