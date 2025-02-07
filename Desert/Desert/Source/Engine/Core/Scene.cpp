#include <Engine/Core/Scene.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer )
         : m_SceneName( sceneName ), m_SceneRenderer( sceneRenderer )
    {
    }

    void Scene::OnUpdate()
    {
        m_SceneRenderer->BeginFrame();
        m_SceneRenderer->EndFrame();
    }

    void Scene::Init()
    {
        m_SceneRenderer->Init();
    }

} // namespace Desert::Core