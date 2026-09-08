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
#include <rflcpp/rfl/Generic.hpp>
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
    // `struct` and not `class`: the definition in Graphic/Environment/SceneEnvironment.hpp uses struct, and the
    // Microsoft C++ ABI encodes the class-key into the decorated name, so the mismatch is a Windows-only link
    // error waiting for the day this forward declaration is the one a caller sees first.
    struct Environment;
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
        Scene();
        Scene( std::string&& sceneName, Graphic::SceneRenderer* sceneRenderer );
        ~Scene();

        // Copying or moving a Scene would put a second entry in (or a stale entry into) the live-scene
        // list below, and nothing has ever done either — the editor holds them by shared_ptr. Said out
        // loud rather than left to the compiler, because the list makes it newly load-bearing.
        Scene( const Scene& )            = delete;
        Scene& operator=( const Scene& ) = delete;
        Scene( Scene&& )                 = delete;
        Scene& operator=( Scene&& )      = delete;

        /**
         * @brief EVERY WORLD THAT IS CURRENTLY ALIVE, in creation order.
         *
         * WHAT IT IS FOR. Asset eviction has to know what is still needed, and "what is still needed" is a
         * property of every live world, not of the one that just changed. A running editor holds several:
         * the level, each extra scene view, the Details preview, the Material Editor's preview ball, the
         * thumbnail renderer's offscreen world. A sweep that consulted only the scene being replaced would
         * release the assets a preview is drawing THIS FRAME — and the preview would come back grey with
         * nothing in the log, because a rebuild-on-miss is silent by design.
         *
         * WHY THE LIST LIVES HERE AND NOT IN THE EDITOR. The editor is the only thing that knows how many
         * scenes it has open, and the engine cannot ask it: EditorLayer is above this layer. A scene
         * registering itself is the only arrangement in which "every live scene" is answerable from the
         * engine at all, and it is answerable for the packaged Runtime by the same code.
         *
         * Raw pointers, non-owning: a scene is IN this list exactly between its constructor and its
         * destructor, so an entry can never be dangling and nothing here extends a lifetime.
         */
        [[nodiscard]] static const std::vector<Scene*>& LiveScenes();

        void Clear();

        [[nodiscard]] Common::BoolResultStr BeginScene();
        void                                OnUpdate( const Common::Timestep& ts );
        [[nodiscard]] Common::BoolResultStr EndScene();

        [[nodiscard]] Common::BoolResultStr Init();

        // Has Init() ever run on this scene? Asked by a caller that DEFERRED the first Init and now has
        // to know whether the deferred one actually happened — a scene load can refuse the file and
        // return with nothing initialised, and rendering a scene whose renderer has no systems is not a
        // recoverable state. Answered by the scene rather than tracked by the caller, because the caller
        // would be a second place holding the same fact (contract §2, one source of truth per value).
        //
        // Stays true across Clear() and across a second Init(): the question is "does this scene have a
        // renderer that has been built", and Clear() empties the world without touching the renderer.
        [[nodiscard]] bool IsInitialized() const
        {
            return m_Initialized;
        }

        const std::shared_ptr<Graphic::Image2D>     GetFinalImage() const;
        const std::shared_ptr<Graphic::Framebuffer> GetTargetFramebuffer() const;

        ECS::Entity& CreateNewEntity( std::string&& entityName );
        ECS::Entity& CreateEntityWithUUID( const Common::UUID& uuid, const std::string& name );

        [[nodiscard]] const auto& GetAllEntities() const
        {
            return m_Entitys;
        }

        void Resize( const uint32_t width, const uint32_t height ) const;

        // By value — see SceneRenderer::GetEnvironment(). The renderer composes this answer rather than
        // storing it, so a reference here would outlive the object it names.
        [[nodiscard]] std::optional<Graphic::Environment> GetEnvironment() const;

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

        // THE FILE THIS SCENE WAS LOADED FROM, AS IT WAS PARSED — kept for one purpose and read at one
        // place: SceneSerializer merges what it is about to write onto this, so a key the file carries
        // and this build does not declare survives the save (Serialize/ForeignKeys.hpp).
        //
        // THIS IS NOT A SECOND SOURCE OF TRUTH, and the reason is structural rather than a promise. The
        // merge only ever takes a key that the WRITER DID NOT PRODUCE, and the writer produces every
        // key this build knows about — so nothing that reaches the file from here is a value the engine
        // has any state for. There is no path by which a value is read out of this and used; the one
        // accessor hands the whole tree to the merge and nothing else calls it.
        //
        // Empty for a scene that was never loaded from a file (File → New), which makes the merge the
        // identity and costs a new scene nothing.
        [[nodiscard]] const rfl::Generic::Object& GetLoadedDocument() const
        {
            return m_LoadedDocument;
        }
        void SetLoadedDocument( rfl::Generic::Object document )
        {
            m_LoadedDocument = std::move( document );
        }

        // The engine's ONE "save this scene" entry point — and therefore the one that has to answer
        // whether the save happened. It used to return void into a void (SceneSerializer::SaveToFile),
        // so the editor could only assume; see SceneSerializer::SaveToFile for what that cost.
        [[nodiscard]] Common::BoolResultStr Serialize( const Assets::AssetManager* assetManager ) const;

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

        // Creates every component pool a system may touch, serially, before any parallel group opens.
        // EnTT creates a pool on FIRST touch and that creation writes to the registry even through a
        // const reference, so two collectors first-touching a type on two threads race on a std::vector.
        // See the definition for the mechanism and the test that keeps the list honest.
        void PrepareComponentPools();

        // Runs the ECS systems: sequential by default, but maximal runs of CanRunParallel() systems
        // execute concurrently on the JobSystem — each system writes its OWN command buffer, so no
        // system ever contends on the (single-threaded) arena allocator.
        void ExecuteSystems( const Common::Timestep& gameplayTs );

    private:
        entt::registry m_Registry;

        std::vector<std::unique_ptr<ECS::System>> m_Systems;

        // The last "these directional lights emit nothing" complaint this scene printed, kept so the
        // per-frame collector reports each distinct state ONCE instead of sixty times a second. Empty
        // means "nothing to complain about", which is also the state whose arrival is worth a line.
        std::string m_DegenerateDirLightsReported;

        std::vector<ECS::Entity>                 m_Entitys;
        std::unordered_map<Common::UUID, size_t> m_EntitysMap;

        // Set by Init(), never cleared — see IsInitialized().
        bool m_Initialized = false;

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
        // See GetLoadedDocument() — the parsed .desce, held only so the saver can keep the keys this
        // build cannot name.
        rfl::Generic::Object m_LoadedDocument;
    };
} // namespace Desert::Core