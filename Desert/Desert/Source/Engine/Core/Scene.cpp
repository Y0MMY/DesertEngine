#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer )
         : m_SceneName( sceneName ), m_SceneRenderer( sceneRenderer )
    {
    }

    void Scene::BeginScene( const Core::Camera& camera )
    {
        m_SceneRenderer->BeginScene( shared_from_this(), camera );
    }

    void Scene::Init()
    {
        m_SceneRenderer->Init();
    }

    void Scene::SetEnvironment( const std::shared_ptr<Graphic::Image2D>& environment )
    {
        m_Environment = environment;
    }

    void Scene::AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const
    {
        m_SceneRenderer->RenderMesh( mesh );
    }

    void Scene::EndScene()
    {
        m_SceneRenderer->EndScene();
    }

} // namespace Desert::Core