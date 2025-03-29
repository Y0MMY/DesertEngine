#pragma once

#include <Engine/Graphic/Image.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
}

namespace Desert
{
    class Mesh;
}

namespace Desert::Core
{
    class Scene final : public std::enable_shared_from_this<Scene>
    {
    public:
        Scene() = default;
        Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer );

        void BeginScene( const Core::Camera& camera );
        void EndScene();
        void Init();

        void SetEnvironment( const std::shared_ptr<Graphic::Image2D>& environment );

        void AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const;

        const std::shared_ptr<Graphic::Image2D>& GetEnvironment() const
        {
            return m_Environment;
        }

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;

        std::shared_ptr<Graphic::Image2D> m_Environment;
    };
} // namespace Desert::Core