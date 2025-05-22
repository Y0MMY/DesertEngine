#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Common/Core/Core.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
    class Environment;
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

        [[nodiscard]] Common::BoolResult BeginScene( const Core::Camera& camera );
        [[nodiscard]] Common::BoolResult EndScene();

        [[nodiscard]] Common::BoolResult Init();

        [[nodiscard]] const Graphic::Environment CreateEnvironment( const Common::Filepath& filepath );

        void SetEnvironment( const std::shared_ptr<Graphic::ImageCube>& environment );

        void AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const;

        [[nodiscard]] const std::shared_ptr<Graphic::ImageCube>& GetEnvironment() const
        {
            return m_Environment;
        }

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;

        std::shared_ptr<Graphic::ImageCube> m_Environment;
    };
} // namespace Desert::Core