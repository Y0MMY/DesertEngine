#pragma once

#include <Common/Core/ResultStr.hpp>

namespace Desert::Graphic
{
    class DynamicResources
    {
    public:
        virtual ~DynamicResources() = default;

        [[nodiscard]] virtual Common::BoolResultStr Invalidate() = 0;
        [[nodiscard]] virtual Common::BoolResultStr Release()    = 0;
    };

} // namespace Desert::Graphic