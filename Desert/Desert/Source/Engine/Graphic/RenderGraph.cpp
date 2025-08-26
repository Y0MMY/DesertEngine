#include "RenderGraph.hpp"

#include "Renderer.hpp"

namespace Desert::Graphic
{

    void RenderGraph::AddPass( std::string&& name, std::function<void()>&& execute,
                               std::shared_ptr<RenderPass>&& renderPass )
    {

        m_Renderpass = std::move( renderPass );
        m_Passes.push_back( { std::move( name ), std::move( execute ) } );
    }

    void RenderGraph::Execute()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( m_Renderpass );
        for ( auto& pass : m_Passes )
        {
            pass.Execute();
        }
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic