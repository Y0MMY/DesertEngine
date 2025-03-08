#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer )
         : m_SceneName( sceneName ), m_SceneRenderer( sceneRenderer )
    {
    }

    void Scene::OnUpdate(const Core::Camera& camera)
    {
        m_SceneRenderer->BeginScene( shared_from_this(), camera );
        m_SceneRenderer->EndScene();
    }

    void Scene::Init()
    {
        m_SceneRenderer->Init();
    }

    void Scene::SetEnvironment( const std::shared_ptr<Graphic::Image2D>& environment )
    {
        m_Environment = environment;
    }

} // namespace Desert::Core