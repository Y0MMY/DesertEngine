#pragma once

#include <Common/Core/Result.hpp>

namespace Desert::Graphic
{
    class DynamicResources
    {
    public:
        virtual ~DynamicResources() = default;

        [[NODISCARD]] virtual Common::BoolResult Invalidate() = 0;
        [[NODISCARD]] virtual Common::BoolResult Release()    = 0;
    };

} // namespace Desert::Graphic