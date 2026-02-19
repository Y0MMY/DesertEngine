#pragma once

#include <cstdint>

namespace Common::Core
{
    struct Handle
    {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool IsValid() const
        {
            return Generation != 0;
        }

        bool operator==( const Handle& other ) const
        {
            return Index == other.Index && Generation == other.Generation;
        }
    };
} // namespace Common::Core
