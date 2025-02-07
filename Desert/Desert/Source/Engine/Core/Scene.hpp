#pragma once

#include <Engine/Graphic/SceneRenderer.hpp>

namespace Desert::Core
{
    class Scene final
    {
    public:
        Scene() = default;
        Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer );

        void OnUpdate();
        void Init();

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;
    };
} // namespace Desert::Core