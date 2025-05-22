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
        return m_SceneRenderer->Init();
    }

    void Scene::SetEnvironment( const std::shared_ptr<Graphic::ImageCube>& environment )
    {
        m_Environment = environment;
    }

    void Scene::AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const
    {
        m_SceneRenderer->RenderMesh( mesh );
    }

    NO_DISCARD Common::BoolResult Scene::EndScene()
    {
        return m_SceneRenderer->EndScene();
    }

    NO_DISCARD const Graphic::Environment Scene::CreateEnvironment( const Common::Filepath& filepath )
    {
        return m_SceneRenderer->CreateEnvironment( filepath );
    }

} // namespace Desert::Core