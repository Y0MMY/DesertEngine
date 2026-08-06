#pragma once

#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/RenderPass.hpp>
#include <Engine/Graphic/ExternalRenderPass.hpp>

#include <Common/Core/Core.hpp>
#include <Engine/Core/Camera.hpp>

#include "SceneSettings.hpp"

#include <Common/Core/ResultStr.hpp>
#include <Common/Core/Timestep.hpp>
#include <Common/Core/UUID.hpp>
#include <glm/glm.hpp>
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

namespace Common::Math
{
    class Ray;
}

namespace Desert::Core
{
    // Result of Scene::Raycast — nearest static-mesh hit (world space).
    struct RaycastHit
    {
        bool         Hit      = false;
        Common::UUID Entity;                       // hit entity's UUID (valid only when Hit)
        glm::vec3    Point    = glm::vec3( 0.0f );  // world hit point
        glm::vec3    Normal   = glm::vec3( 0.0f, 1.0f, 0.0f ); // world box-face normal
        float        Distance = 0.0f;
    };

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

        [[nodiscard]] const auto& GetRegistry() const
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

        // Ray vs every StaticMeshComponent's submesh AABBs (world space). Returns the nearest hit
        // (entity/point/normal). Engine-owned so picking AND tools (foliage placement, etc.) share ONE
        // raycast + ONE mesh resolution (MeshHandle / RuntimeMesh / primitive) instead of duplicating both.
        [[nodiscard]] bool Raycast( const Common::Math::Ray& ray, RaycastHit& outHit ) const;

        // Play-mode state. Edit = authoring (gameplay systems frozen); Play = running (gameplay ticks);
        // Paused = running but time frozen (ts forced to 0). The editor snapshots the scene on Play and
        // restores it on Stop, so play-time changes don't corrupt the authored scene.
        enum class SceneState
        {
            Edit,
            Play,
            Paused
        };

        [[nodiscard]] SceneState GetState() const { return m_State; }
        void                     SetState( SceneState state ) { m_State = state; }
        [[nodiscard]] bool       IsPlaying() const { return m_State == SceneState::Play; }

        [[nodiscard]] SceneSettings& GetSettings()
        {
            return m_Settings;
        }

        [[nodiscard]] const SceneSettings& GetSettings() const
        {
            return m_Settings;
        }

        void Serialize( const Assets::AssetManager* assetManager ) const;

        // Editor Pass API: inject a render pass into the scene render graph from outside the engine
        // (debug draw, gizmos, authoring aids). See Graphic::ExternalPassSpecification for placement.
        void RegisterExternalPass( Graphic::ExternalPassSpecification&& spec );
        void UnregisterExternalPass( const std::string& name );

        const std::weak_ptr<Core::Camera>& GetMainCamera() const
        {
            return m_MainCamera;
        }

        // The scene renders through whatever camera is set active here. The editor sets its EditorCamera in
        // Edit mode and a GameplayCamera (from the main CameraComponent) in Play mode. The scene owns the
        // active camera so the view never depends on a scene CameraComponent existing (Init() defaults to an
        // EditorCamera, so a brand-new scene still has a working viewport).
        void SetActiveCamera( const std::shared_ptr<Core::Camera>& camera )
        {
            m_ActiveCamera = camera;
            m_MainCamera   = m_ActiveCamera;
        }
        [[nodiscard]] const std::shared_ptr<Core::Camera>& GetActiveCamera() const { return m_ActiveCamera; }

        // Render through THIS camera and stop choosing one per play state. For a scene whose view is driven
        // from outside — the Details preview orbits its own GameplayCamera — a plain SetActiveCamera lasts
        // exactly until the next OnUpdate, which puts the scene's EditorCamera back. That camera polls the
        // global mouse/keyboard, so the preview then flew along with the real viewport. Pinning also mutes
        // the scene's EditorCamera input, since nothing is driving it any more. Pass nullptr to unpin.
        void               PinActiveCamera( const std::shared_ptr<Core::Camera>& camera );
        [[nodiscard]] bool HasPinnedCamera() const
        {
            return m_CameraPinned;
        }

        template <typename T, typename... Args>
        void AddSystem( Args&&... args )
        {
            DESERT_VERIFY( (std::is_base_of_v<ECS::System, T>));
            m_Systems.emplace_back( std::make_unique<T>( std::forward<Args>( args )... ) );
        }

        void Attach( ECS::Entity parent, ECS::Entity child );

        // Removes the child from its parent (if any) and makes it a root entity.
        void Detach( ECS::Entity child );

        void DestroyEntity( ECS::Entity entity );

        // Sets VisibilityComponent on the entity and its entire subtree (UE-like hierarchical visibility).
        void SetVisibleRecursive( ECS::Entity entity, bool visible );

    private:
        void FindMainCamera();
        void OnEntityCreated_Camera();

        // Picks the active camera from the play state (Edit -> EditorCamera, Play -> the main
        // CameraComponent). Skipped entirely while a camera is pinned.
        void UpdateActiveCameraSource();

        void SetupRegistryCallbacks();

        // Runs the ECS systems: sequential by default, but maximal runs of CanRunParallel() systems
        // execute concurrently on the JobSystem — each system writes its OWN command buffer, so no
        // system ever contends on the (single-threaded) arena allocator.
        void ExecuteSystems( const Common::Timestep& gameplayTs );

    private:
        entt::registry m_Registry;

        std::vector<std::unique_ptr<ECS::System>> m_Systems;

        std::vector<ECS::Entity>                 m_Entitys;
        std::unordered_map<Common::UUID, size_t> m_EntitysMap;

        Graphic::SceneRenderer*     m_SceneRenderer;
        std::weak_ptr<Core::Camera>   m_MainCamera;   // non-owning view (renderer reads this)
        std::shared_ptr<Core::Camera> m_ActiveCamera; // owns the current camera (editor or gameplay)
        std::shared_ptr<Core::Camera> m_EditorCamera;   // persistent editor view (Edit mode)
        bool                          m_CameraPinned = false; // view driven from outside (see PinActiveCamera)
        std::shared_ptr<Core::Camera> m_GameplayCamera; // persistent game view (Play mode), driven by the
                                                        // main CameraComponent
        mutable uint32_t              m_ViewportWidth  = 1280;
        mutable uint32_t              m_ViewportHeight = 720;
        SceneState                    m_State = SceneState::Edit;

        // One command buffer PER system (index-matched to m_Systems): parallel systems record without
        // sharing the arena; buffers are executed in registration order, so the frame's draw order is
        // identical to the old single-buffer sequential path.
        std::vector<std::unique_ptr<Graphic::Render::RenderCommandBuffer>> m_SystemCommandBuffers;

        SceneSettings m_Settings;
        std::string   m_SceneName;
    };
} // namespace Desert::Core