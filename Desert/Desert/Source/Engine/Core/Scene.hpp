#pragma once

#include <Engine/Graphic/Image.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
}

namespace Desert::Core
{
    class Scene final : public std::enable_shared_from_this<Scene>
    {
    public:
        Scene() = default;
        Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer );

        void OnUpdate(const Core::Camera& camera);
        void Init();

        void SetEnvironment( const std::shared_ptr<Graphic::Image2D>& environment );

        const std::shared_ptr<Graphic::Image2D>& GetEnvironment() const { return m_Environment; }

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;

        std::shared_ptr<Graphic::Image2D> m_Environment;
    };
} // namespace Desert::Core