#pragma once

#include <entt/entt.hpp>

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
    namespace ECS
    {
        class Entity;
    }

} // namespace Desert

namespace Desert::Core
{
    class SceneRendererManager // TODO: move to good space
    {
    public:
        static inline std::vector<std::shared_ptr<Graphic::SceneRenderer>> SceneRenderers;
    };

    class Scene final : public std::enable_shared_from_this<Scene>
    {
    public:
        Scene() = default;
        Scene( const std::string& sceneName, const std::shared_ptr<Graphic::SceneRenderer>& sceneRenderer );

        [[nodiscard]] Common::BoolResult BeginScene( const Core::Camera& camera );
        void                             OnUpdate();
        [[nodiscard]] Common::BoolResult EndScene();

        [[nodiscard]] Common::BoolResult Init();

        [[nodiscard]] const Graphic::Environment CreateEnvironment( const Common::Filepath& filepath );

        void SetEnvironment( const Graphic::Environment& environment );

        void AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh ) const;

        ECS::Entity& CreateNewEntity( std::string&& entityName );

        [[nodiscard]] const auto& GetAllEntities() const
        {
            return m_Entitys;
        }

        [[nodiscard]] const Graphic::Environment& GetEnvironment() const;

        [[nodiscard]] auto& GetRegistry()
        {
            return m_Registry;
        }

        [[nodiscard]] auto& GetSceneName()
        {
            return m_SceneName;
        }

    private:
        std::string                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;
        entt::registry                          m_Registry;
        std::vector<ECS::Entity>                m_Entitys;
    };
} // namespace Desert::Core