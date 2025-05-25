#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Common/Core/Core.hpp>

#include <Engine/Core/Camera.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
    class Environment;
} // namespace Desert::Graphic

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

        void SetEnvironment( const Graphic::Environment& environment );

        void AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const;

        [[nodiscard]] const Graphic::Environment& GetEnvironment() const;

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;
    };
} // namespace Desert::Core