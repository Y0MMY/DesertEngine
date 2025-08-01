#pragma once

#include "RenderPass.hpp"

namespace Desert::Graphic
{
    class RenderGraph
    {
    public:
        struct Pass
        {
            std::string                 Name;
            std::function<void()>       Execute;
            std::shared_ptr<RenderPass> Renderpass;
        };

        void AddPass( std::string&& name, std::function<void()>&& execute,
                      std::shared_ptr<RenderPass>&& renderPass );
        void Execute();

    private:
        std::vector<Pass> m_Passes;
    };
} // namespace Desert::Graphic