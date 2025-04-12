#include "EditorLayer.hpp"

namespace Desert
{
    EditorLayer::EditorLayer( const std::string& layerName ) : Common::Layer( layerName )
    {
        m_testscenerenderer = std::make_shared<Graphic::SceneRenderer>();
        m_testscene         = std::make_shared<Core::Scene>( "sdfsdf", m_testscenerenderer );
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnAttach()
    {
        m_testscene->Init();
        m_Environment          = Graphic::TextureCube::Create( "output123.hdr" );
        const auto& textureRes = m_Environment->Invalidate();
        if ( !textureRes )
        {
            // return Common::MakeError( textureRes.GetError() );
        }
        m_testscene->SetEnvironment( m_Environment->GetImage2D() );

        m_Mesh = std::make_shared<Mesh>( "Cube1m.fbx" );
        m_Mesh->Invalidate();
        m_testscene->AddMeshToRenderList( m_Mesh );

        return BOOLSUCCESS;
    }

    [[nodiscard]] Common::BoolResult EditorLayer::OnUpdate( Common::Timestep ts )
    {
        m_EditorCamera.OnUpdate();
        const auto&& beginResult = m_testscene->BeginScene( m_EditorCamera );
        if ( !beginResult )
        {
            return Common::MakeError( beginResult.GetError() );
        }
        const auto&& endResult = m_testscene->EndScene();
        if ( !endResult )
        {
            return Common::MakeError( endResult.GetError() );
        }

        return BOOLSUCCESS;
    }

} // namespace Desert