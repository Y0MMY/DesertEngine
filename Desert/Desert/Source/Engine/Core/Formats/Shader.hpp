#pragma once

#include <cstdint>

namespace Desert::Core::Formats
{
    enum class ShaderStage
    {
        None     = 0,
        Vertex   = 0x00000001,
        Fragment = 0x00000010,
        Compute  = 0x00000020
    };

    enum class ShaderPragmas
    {
        None = 0,
    };

    enum class ShaderValueType : uint8_t
    {
        Unknown = 0,

        Float,
        Float2,
        Float3,
        Float4,

        Int,
        Int2,
        Int3,
        Int4,

        UInt,
        Bool,

        Mat3,
        Mat4,

        Struct
    };

    enum class ShaderResourceType
    {
        Sampler1D,
        Sampler2D,
        Sampler3D,
        SamplerCube
    };

    enum class BufferKind : uint8_t
    {
        Uniform,
        Storage,
        PushConstant
    };

} // namespace Desert::Core::Formats