#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/RenderPass.hpp>

#include <Common/Core/Core.hpp>
#include <Engine/Core/Camera.hpp>

#include "SceneSettings.hpp"

#include <Common/Core/ResultStr.hpp>
#include <Common/Core/Timestep.hpp>
#include <Common/Core/UUID.hpp>
#include <cstdint>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/System/System.hpp>
#include <Engine/Graphic/Framebuffer.hpp>
#include <entt/entt.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
        Scene( std::string&& sceneName, Graphic::SceneRenderer* sceneRenderer );
        ~Scene() = default;

        void Clear();

        [[nodiscard]] Common::BoolResultStr BeginScene();
        void                                OnUpdate( const Common::Timestep& ts );
        [[nodiscard]] Common::BoolResultStr EndScene();

        [[nodiscard]] Common::BoolResultStr Init();

        [[nodiscard]] const Graphic::Environment CreateEnvironment( const Common::Filepath& filepath );

        const std::shared_ptr<Graphic::Image2D>     GetFinalImage() const;
        const std::shared_ptr<Graphic::Framebuffer> GetTargetFramebuffer() const;

        ECS::Entity& CreateNewEntity( std::string&& entityName );
        ECS::Entity& CreateEntityWithUUID( const Common::UUID& uuid, const std::string& name );

        [[nodiscard]] const auto& GetAllEntities() const
        {
            return m_Entitys;
        }

        void Resize( const uint32_t width, const uint32_t height ) const;

        [[nodiscard]] const std::optional<Graphic::Environment>& GetEnvironment() const;

        [[nodiscard]] Graphic::SceneRenderer* GetSceneRenderer() const
        {
            return m_SceneRenderer;
        }

        [[nodiscard]] auto& GetRegistry()
        {
            return m_Registry;
        }

        [[nodiscard]] auto& GetSceneName()
        {
            return m_SceneName;
        }

        void SetSceneName( const std::string& name )
        {
            m_SceneName = name;
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

        void Serialize( const Assets::AssetManager* assetManager ) const;

        void RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                   std::shared_ptr<Graphic::RenderPass>&& renderPass );

        const std::weak_ptr<Core::Camera>& GetMainCamera() const
        {
            return m_MainCamera;
        }

        template <typename T, typename... Args>
        void AddSystem( Args&&... args )
        {
            DESERT_VERIFY( (std::is_base_of_v<ECS::System, T>));
            m_Systems.emplace_back( std::make_unique<T>( std::forward<Args>( args )... ) );
        }

        void Attach( ECS::Entity parent, ECS::Entity child );

        void DestroyEntity( ECS::Entity entity );

    private:
        void FindMainCamera();
        void OnEntityCreated_Camera();

        void SetupRegistryCallbacks();

    private:
        entt::registry m_Registry;

        std::vector<std::unique_ptr<ECS::System>> m_Systems;

        std::vector<ECS::Entity>                 m_Entitys;
        std::unordered_map<Common::UUID, size_t> m_EntitysMap;

        Graphic::SceneRenderer*     m_SceneRenderer;
        std::weak_ptr<Core::Camera> m_MainCamera;

        std::unique_ptr<Graphic::Render::RenderCommandBuffer> m_CommandBuffer;

        SceneSettings m_Settings;
        std::string   m_SceneName;
    };
} // namespace Desert::Core