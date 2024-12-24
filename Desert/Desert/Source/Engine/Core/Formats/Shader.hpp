#pragma once

namespace Desert::Core::Formats
{
    enum class ShaderStage
    {
        None     = 0,
        Vertex   = 1,
        Fragment = 2,
        Compute  = 3
    };

    enum class ShaderPragmas
    {
        None = 0,
    };

    enum class ShaderDataType
    {
        None = 0,
        Float,
        Float2,
        Float3,
        Float4,
        Int,
        Int2,
        Int3,
        Int4,
        UInt,
        Mat3,
        Mat4,
        Bool,

        Sampler1D,
        Sampler2D,
        Sampler3D,

        Struct
    };
}