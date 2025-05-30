#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer )
         : m_SceneName( sceneName ), m_SceneRenderer( sceneRenderer )
    {
    }

    NO_DISCARD Common::BoolResult Scene::BeginScene( const Core::Camera& camera )
    {
        return m_SceneRenderer->BeginScene( shared_from_this(), camera );
    }

    NO_DISCARD Common::BoolResult Scene::Init()
    {
        const auto res = m_SceneRenderer->Init();
        if ( !res )
        {
            return Common::MakeError( res.GetError() );
        }

        SceneRendererManager::SceneRenderers.push_back( m_SceneRenderer );

        return BOOLSUCCESS;
    }

    void Scene::SetEnvironment( const Graphic::Environment& environment )
    {
        m_SceneRenderer->SetEnvironment( environment );
    }

    void Scene::AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const
    {
        m_SceneRenderer->RenderMesh( mesh );
    }

    void Scene::OnUpdate()
    {
        m_SceneRenderer->OnUpdate();
    }

    NO_DISCARD Common::BoolResult Scene::EndScene()
    {
        return m_SceneRenderer->EndScene();
    }

    NO_DISCARD const Graphic::Environment Scene::CreateEnvironment( const Common::Filepath& filepath )
    {
        return m_SceneRenderer->CreateEnvironment( filepath );
    }

    const Desert::Graphic::Environment& Scene::GetEnvironment() const
    {
        return m_SceneRenderer->GetEnvironment();
    }

    Desert::ECS::Entity Scene::CreateNewEntity( std::string&& entityName )
    {
        return ECS::Entity( std::move( entityName ), m_Registry.create() );
    }

} // namespace Desert::Core