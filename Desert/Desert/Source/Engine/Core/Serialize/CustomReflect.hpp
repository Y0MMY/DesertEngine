#pragma once

#include <Common/Core/UUID.hpp>
#include <Common/Core/AssetHandle.hpp>
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

    // AssetHandle is a distinct type from UUID (own default/derivation semantics) so it needs its own
    // reflector. Serializes as the SAME bare uint64 as before, so existing .demat / cooked assets that
    // stored a raw handle number round-trip unchanged.
    template <>
    struct Reflector<::Common::AssetHandle>
    {
        using ReflType = uint64_t;

        static ::Common::AssetHandle to( const ReflType& val ) noexcept
        {
            return ::Common::AssetHandle( val );
        }

        static ReflType from( const ::Common::AssetHandle& handle ) noexcept
        {
            return (uint64_t)handle;
        }
    };
} // namespace rfl