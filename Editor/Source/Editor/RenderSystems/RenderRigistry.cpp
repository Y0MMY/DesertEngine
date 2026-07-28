#include "RenderRigistry.hpp"

namespace Desert::Editor::Render
{
    RenderRegistry::RenderRegistry( const std::shared_ptr<Core::Scene>& scene ) : m_Scene( scene )
    {
        m_GridPass = std::make_unique<EditorGridPass>();
        if ( const auto result = m_GridPass->Install( scene ); !result )
        {
            LOG_WARN( "[RenderRegistry] {}", result.GetError() );
            m_GridPass.reset();
        }

        m_ColliderPass = std::make_unique<EditorColliderPass>();
        if ( const auto result = m_ColliderPass->Install( scene ); !result )
        {
            LOG_WARN( "[RenderRegistry] {}", result.GetError() );
            m_ColliderPass.reset();
        }
    }

    void RenderRegistry::Render()
    {
        // Per-frame editor draws that DON'T go through the render graph would go here. The graph-injected
        // passes (grid) execute inside SceneRenderer::OnUpdate on their own.
    }

} // namespace Desert::Editor::Render
