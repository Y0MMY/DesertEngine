#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/Models/SceneRendererUpdate.hpp>
#include <Engine/Graphic/Models/MeshRenderData.hpp>

#include <Common/Core/Core.hpp>
#include <Engine/Core/Camera.hpp>

#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Runtime/ResourceResolver.hpp>

#include <Engine/ECS/System/System.hpp>

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

    class Scene final : public std::enable_shared_from_this<Scene>
    {
    public:
        Scene() = default;
        Scene( std::string&& sceneName, const std::shared_ptr<Runtime::RuntimeResourceManager>& resourceManager );

        [[nodiscard]] Common::BoolResult BeginScene( const Core::Camera& camera );
        void                             OnUpdate( const Common::Timestep& ts );
        [[nodiscard]] Common::BoolResult EndScene();

        [[nodiscard]] Common::BoolResult Init();

        void Shutdown();

        [[nodiscard]] const Graphic::Environment CreateEnvironment( const Common::Filepath& filepath );

        const std::shared_ptr<Graphic::Image2D> GetFinalImage() const;

        ECS::Entity& CreateNewEntity( std::string&& entityName );

        [[nodiscard]] const auto& GetAllEntities() const
        {
            return m_Entitys;
        }

        void Resize( const uint32_t width, const uint32_t height ) const;

        [[nodiscard]] const Graphic::Environment& GetEnvironment() const;

        [[nodiscard]] auto& GetRegistry()
        {
            return m_Registry;
        }

        [[nodiscard]] auto& GetSceneName()
        {
            return m_SceneName;
        }

        [[nodiscard]] std::optional<std::reference_wrapper<const ECS::Entity>>
        FindEntityByID( const Common::UUID& uuid ) const;

        void Serialize() const;

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const uint32_t system, Args&&... args )
        {
            m_Systems[system] = std::make_unique<System>( std::forward<Args>( args )... );
        }

    private:
        static constexpr uint32_t SYSTEMS_COUNT = 2U;

        std::string                                             m_SceneName;
        std::shared_ptr<Graphic::SceneRenderer>                 m_SceneRenderer;
        const std::shared_ptr<Runtime::ResourceResolver>        m_ResourceResolver;
        std::array<std::unique_ptr<ECS::System>, SYSTEMS_COUNT> m_Systems;

        entt::registry                           m_Registry;
        std::vector<ECS::Entity>                 m_Entitys;
        std::unordered_map<Common::UUID, size_t> m_EntitysMap;
    };
} // namespace Desert::Core