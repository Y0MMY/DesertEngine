#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/RenderPass.hpp>

#include <Common/Core/Core.hpp>
#include <Engine/Core/Camera.hpp>

#include "SceneSettings.hpp"

#include <Engine/Assets/AssetManager.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/System/System.hpp>

namespace Desert::Graphic
{
    class SceneRenderer;
    class Environment;
} // namespace Desert::Graphic

namespace Desert::Core
{
    class Scene final
    {
    public:
        Scene() = default;
        Scene( std::string&& sceneName );
        ~Scene() = default;

        [[nodiscard]] Common::BoolResultStr BeginScene();
        void                                OnUpdate( const Common::Timestep& ts );
        [[nodiscard]] Common::BoolResultStr EndScene();

        [[nodiscard]] Common::BoolResultStr Init();

        [[nodiscard]] const Graphic::Environment CreateEnvironment( const Common::Filepath& filepath );

        const std::shared_ptr<Graphic::Image2D>     GetFinalImage() const;
        const std::shared_ptr<Graphic::Framebuffer> GetTargetFramebuffer() const;

        ECS::Entity& CreateNewEntity( std::string&& entityName );

        [[nodiscard]] const auto& GetAllEntities() const
        {
            return m_Entitys;
        }

        void Resize( const uint32_t width, const uint32_t height ) const;

        [[nodiscard]] const std::optional<Graphic::Environment>& GetEnvironment() const;

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

        [[nodiscard]] SceneSettings& GetSettings()
        {
            return m_Settings;
        }

        [[nodiscard]] const SceneSettings& GetSettings() const
        {
            return m_Settings;
        }

        void Serialize() const;

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<Graphic::RenderPass>&& renderPass );

        const std::weak_ptr<Core::Camera>& GetMainCamera() const
        {
            return m_MainCamera;
        }

    private:
        template <typename System, typename... Args>
        void RegisterSystem( const uint32_t system, Args&&... args )
        {
            m_Systems[system] = std::make_unique<System>( std::forward<Args>( args )... );
        }

    private:
        void FindMainCamera();
        void OnEntityCreated_Camera();

    private:
        entt::registry m_Registry;

        static constexpr uint32_t                               SYSTEMS_COUNT = 4U;
        std::array<std::unique_ptr<ECS::System>, SYSTEMS_COUNT> m_Systems;

        std::vector<ECS::Entity>                 m_Entitys;
        std::unordered_map<Common::UUID, size_t> m_EntitysMap;

        std::shared_ptr<Graphic::SceneRenderer> m_SceneRenderer;
        std::weak_ptr<Core::Camera>             m_MainCamera;

        SceneSettings m_Settings;
        std::string   m_SceneName;
    };
} // namespace Desert::Core