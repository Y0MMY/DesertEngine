#pragma once

#include <Common/Core/Handle.hpp>

namespace Desert::Runtime
{
    struct ImageHandle
    {
        enum class Type : uint8_t
        {
            Image2D,
            ImageCube
        };

        ImageHandle() = default;

        explicit ImageHandle( const Common::Core::Handle& value, Type type ) : Value( value ), ImageType( type )
        {
            DESERT_VERIFY( Value.IsValid(), "ImageHandle created with invalid Handle" );
        }

        bool IsValid() const
        {
            return Value.IsValid();
        }

        Common::Core::Handle Value{};
        Type                 ImageType{};
    };

} // namespace Desert::Runtime
