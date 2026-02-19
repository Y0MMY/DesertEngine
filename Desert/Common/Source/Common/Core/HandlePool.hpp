#pragma once

#include <Common/Core/Handle.hpp>
#include <vector>

namespace Common::Core
{
    class HandlePool
    {
    public:
        Handle Allocate();
        void   Release( const Handle& handle );

        void Clear();

    private:
        struct Entry
        {
            uint32_t Generation = 0;
            bool     Alive      = false;
        };

        std::vector<Entry>    m_Entries;
        std::vector<uint32_t> m_FreeList;
    };
} // namespace Common::Core
