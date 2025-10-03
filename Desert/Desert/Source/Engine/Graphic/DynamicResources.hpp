#pragma once

#include <Common/Core/ResultStr.hpp>

namespace Desert::Graphic
{
    class DynamicResources
    {
    public:
        virtual ~DynamicResources() = default;

        [[NODISCARD]] virtual Common::BoolResultStr Invalidate() = 0;
        [[NODISCARD]] virtual Common::BoolResultStr Release()    = 0;
    };

} // namespace Desert::Graphic