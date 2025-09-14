#pragma once

#include "RenderGraphBuilder.hpp"

namespace Desert::Graphic
{
    class IRenderSystem
    {
    public:
        virtual ~IRenderSystem() = default;

        virtual void RegisterPasses( RenderGraphBuilder& builder )                 = 0;
    };
} // namespace Desert::Graphic