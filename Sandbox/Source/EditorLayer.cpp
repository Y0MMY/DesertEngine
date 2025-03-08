#include "EditorLayer.hpp"

namespace Desert
{
    EditorLayer::EditorLayer( const std::string& layerName ) : Common::Layer( layerName )
    {
        m_testscenerenderer = std::make_shared<Graphic::SceneRenderer>();
        m_testscene         = std::make_shared<Core::Scene>( "sdfsdf", m_testscenerenderer );
    }

    void EditorLayer::OnAttach()
    {
        m_testscene->Init();
        m_Environment = Graphic::TextureCube::Create( "Arches_E_PineTree_Radiance.tga" );
        m_testscene->SetEnvironment( m_Environment->GetImage2D() );
    }

    void EditorLayer::OnUpdate( Common::Timestep ts )
    {
        m_EditorCamera.OnUpdate();
        m_testscene->OnUpdate(m_EditorCamera);
    }

} // namespace Desert