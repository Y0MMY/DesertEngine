#pragma once

namespace Desert::Runtime
{
    struct ResourceHandle
    {
        uint32_t Index = ~0u;

        bool IsValid() const
        {
            return Index != InvalidIndex;
        }
        static constexpr uint32_t InvalidIndex = ~0u;

        bool operator==( const ResourceHandle& other ) const
        {
            return Index == other.Index;
        }

        bool operator!=( const ResourceHandle& other ) const
        {
            return !( *this == other );
        }
    };
} // namespace Desert::Runtime