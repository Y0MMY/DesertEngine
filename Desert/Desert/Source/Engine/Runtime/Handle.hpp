#pragma once

namespace Desert::Runtime
{
    struct ResourceHandle
    {
        uint32_t Index;

        bool IsValid() const
        {
            return Index != InvalidIndex;
        }
        static constexpr uint32_t InvalidIndex = ~0u;
    };
} // namespace Desert::Runtime