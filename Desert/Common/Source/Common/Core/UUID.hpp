#pragma once

#include <iostream>
#include <string>

namespace Common
{
    class UUID
    {
    public:
        UUID();
        explicit UUID( uint64_t uuid );
        UUID( const UUID& other );
        explicit UUID( const std::string& uuidStr );

        // Explicit "no UUID" value (0). NOTE: the default ctor mints a RANDOM id, so `UUID{}` must NEVER be
        // used as an invalid/not-found sentinel — use Null()/IsNull() for that.
        static UUID Null()
        {
            return UUID( static_cast<uint64_t>( 0 ) );
        }

        bool IsNull() const
        {
            return m_UUID == 0;
        }

        const std::string ToString() const
        {
            return std::to_string( m_UUID ); // TODO: Cache
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
} // namespace Common

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