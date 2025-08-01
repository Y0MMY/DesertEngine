#include "RenderGraph.hpp"

#include "Renderer.hpp"

namespace Desert::Graphic
{

    void RenderGraph::AddPass( std::string&& name, std::function<void()>&& execute,
                               std::shared_ptr<RenderPass>&& renderPass )
    {

        m_Passes.push_back( { std::move( name ), std::move( execute ), std::move( renderPass ) } );
    }

    void RenderGraph::Execute()
    {
        auto& renderer = Renderer::GetInstance();

        for ( auto& pass : m_Passes )
        {
            renderer.BeginRenderPass( pass.Renderpass );
            pass.Execute();
            renderer.EndRenderPass();
        }
    }

} // namespace Desert::Graphic