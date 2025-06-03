#pragma once

#include <iostream>
#include <string>
#include <cstdint>

namespace Common
{
    class UUID
    {
    public:
        UUID();
        explicit UUID( uint64_t uuid );
        UUID( const UUID& other );

        const std::string ToString() const
        {
            return std::to_string( m_UUID );
        }

        operator uint64_t()
        {
            return m_UUID;
        }
        operator const uint64_t() const
        {
            return m_UUID;
        }

    private:
        uint64_t m_UUID;
    };
} // namespace Radiant

namespace std
{

    template <>
    struct hash<Common::UUID>
    {
        std::size_t operator()( const Common::UUID& uuid ) const
        {
            // uuid is already a randomly generated number, and is suitable as a hash key as-is.
            // this may change in future, in which case return hash<uint64_t>{}(uuid); might be more
            // appropriate
            return uuid;
        }
    };
} // namespace std