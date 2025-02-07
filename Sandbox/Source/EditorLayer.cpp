#include "EditorLayer.hpp"

namespace Desert
{
    EditorLayer::EditorLayer( const std::string& layerName ) : Common::Layer( layerName )
    {
        m_testscenerenderer = std::make_shared<Graphic::SceneRenderer>();
        m_testscene = Core::Scene( "sdfsdf", m_testscenerenderer );
    }

    void EditorLayer::OnAttach()
    {
        m_testscene.Init();
    }

    void EditorLayer::OnUpdate( Common::Timestep ts )
    {
        m_testscene.OnUpdate();
    }

} // namespace Desert