#pragma once

#include <Common/Core/UUID.hpp>
#include <rflcpp/rfl.hpp>
#include <string>

namespace rfl
{
    // Specialize for Common::UUID.
    // Use uint64_t for compatibility with existing cooked mesh assets and serializations.
    template <>
    struct Reflector<::Common::UUID>
    {
        using ReflType = uint64_t;

        static ::Common::UUID to( const ReflType& val ) noexcept
        {
            return ::Common::UUID( val );
        }

        static ReflType from( const ::Common::UUID& uuid ) noexcept
        {
            return (uint64_t)uuid;
        }
    };
} // namespace rfl