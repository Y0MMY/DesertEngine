#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Common/Core/Profiler.hpp>
#include <Common/Core/JobSystem.hpp>
#include <Common/Core/Math/Ray.hpp>

#include <Engine/ECS/Components.hpp>
#include <Engine/Geometry/Mesh.hpp>
#include <Engine/Geometry/SkinnedMesh.hpp>
#include <Engine/Geometry/PrimitiveMeshFactory.hpp>
#include <Engine/Animation/Skeleton.hpp>
#include <Engine/Animation/BoneInfo.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <cfloat>
#include <functional>
#include <limits>
#include <typeinfo>

#include <Engine/Core/Projection.hpp>
#include <Engine/Core/Serialize/SceneSerializer.hpp>

namespace Desert::Core
{
    namespace
    {
        // Resolve a static-mesh component to its renderable Mesh (handle / runtime-edited / shared primitive).
        // This used to live in the editor's ViewportPanel (engine logic that leaked into the editor) — it
        // belongs here, where Raycast and the mesh systems can share it.
        ::Desert::Mesh* ResolveMesh( const ECS::StaticMeshComponent& c )
        {
            if ( c.MeshHandle )
                return Runtime::ResourceRegistry::GetMeshService()->Get( c.MeshHandle );
            if ( c.RuntimeMesh )
                return c.RuntimeMesh.get();
            if ( c.Primitive.has_value() )
                return Geometry::PrimitiveMeshFactory::GetShared( c.Primitive.value() );
            return nullptr;
        }

        // Editor-built runtime rig takes priority over the cooked asset (mirrors the render path).
        ::Desert::Mesh* ResolveSkinnedMesh( const ECS::SkinnedMeshComponent& c )
        {
            if ( c.RuntimeMesh )
                return c.RuntimeMesh.get();
            if ( c.MeshHandle )
                return Runtime::ResourceRegistry::GetMeshService()->Get( c.MeshHandle );
            return nullptr;
        }

        // Bind-pose skinning matrices (skin[i] = chainGlobal_i * OffsetMatrix_i) — identical to the render
        // path's bind branch in MeshECSSystem, so the picked bounds line up with the drawn bind-pose mesh.
        std::vector<glm::mat4> BindSkinningMatrices( const Animation::Skeleton& skeleton )
        {
            const auto&                        bones = skeleton.GetBones();
            std::vector<glm::mat4>             g( bones.size(), glm::mat4( 1.0f ) );
            std::vector<bool>                  done( bones.size(), false );
            std::function<glm::mat4( size_t )> resolve = [&]( size_t i ) -> glm::mat4
            {
                if ( done[i] )
                    return g[i];
                glm::mat4 m = bones[i].LocalBindTransform;
                if ( bones[i].ParentBoneID.has_value() && bones[i].ParentBoneID.value() < bones.size() )
                    m = resolve( bones[i].ParentBoneID.value() ) * bones[i].LocalBindTransform;
                g[i]    = m;
                done[i] = true;
                return m;
            };
            std::vector<glm::mat4> out( bones.size() );
            for ( size_t i = 0; i < bones.size(); ++i )
                out[i] = resolve( i ) * bones[i].OffsetMatrix;
            return out;
        }

        // Mesh-local AABB of a skinned mesh deformed by `skin` (linear blend). A skinned submesh's stored
        // BoundingBox is in RAW-vertex space (which only matches the rendered mesh when bind == identity), so
        // picking must deform the retained CPU vertices by the current pose instead of using that box.
        Common::Math::AABB SkinnedLocalBounds( const SkinnedMesh& mesh, const std::vector<glm::mat4>& skin )
        {
            glm::vec3 mn( FLT_MAX ), mx( -FLT_MAX );
            for ( const auto& sv : mesh.GetVertices() )
            {
                glm::vec3 pos( 0.0f );
                float     wsum = 0.0f;
                for ( size_t j = 0; j < SkinnedVertex::MAX_BONE_INFLUENCES; ++j )
                {
                    const float w = sv.BoneWeights[j];
                    if ( w <= 0.0f )
                        continue;
                    const uint32_t b = sv.BoneIDs[j];
                    if ( b < skin.size() )
                        pos += w * glm::vec3( skin[b] * glm::vec4( sv.StaticVertex.Position, 1.0f ) );
                    wsum += w;
                }
                if ( wsum > 1e-5f )
                    pos /= wsum; // weighted average (robust to weights that don't sum to exactly 1)
                else
                    pos = sv.StaticVertex.Position;
                mn = glm::min( mn, pos );
                mx = glm::max( mx, pos );
            }
            return { mn, mx };
        }
    } // namespace

    bool Scene::Raycast( const Common::Math::Ray& ray, RaycastHit& outHit ) const
    {
        float              closest = std::numeric_limits<float>::max();
        glm::mat4          bestXf( 1.0f );
        Common::Math::AABB bestAABB;
        Common::Math::Ray  bestLocal  = ray;
        float              bestLocalT = 0.0f;
        Common::UUID       bestUUID;
        bool               hit = false;

        for ( const auto& entity : GetAllEntities() )
        {
            ::Desert::Mesh* mesh    = nullptr;
            bool            skinned = false;
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                mesh = ResolveMesh( entity.GetComponent<ECS::StaticMeshComponent>() );
            }
            else if ( entity.HasComponent<ECS::SkinnedMeshComponent>() )
            {
                mesh    = ResolveSkinnedMesh( entity.GetComponent<ECS::SkinnedMeshComponent>() );
                skinned = true;
            }
            if ( !mesh )
                continue;

            const glm::mat4 xf       = entity.GetWorldTransform();
            const auto      localRay = ray.ToLocalSpace( xf );

            if ( !skinned )
            {
                for ( const auto& sm : mesh->GetSubmeshes() )
                {
                    float t = 0.0f;
                    if ( localRay.IntersectsAABB( sm.BoundingBox, t ) && t > 0.0f && t < closest )
                    {
                        closest    = t;
                        bestXf     = xf;
                        bestAABB   = sm.BoundingBox;
                        bestLocal  = localRay;
                        bestLocalT = t;
                        bestUUID   = entity.GetComponent<ECS::UUIDComponent>().UUID;
                        hit        = true;
                    }
                }
            }
            else
            {
                // Skinned: test the POSED mesh bounds (animator pose if any, else bind) — the stored submesh
                // box is raw-vertex space and would miss/mis-hit an imported mesh whose bind != identity.
                auto*                  sk = static_cast<SkinnedMesh*>( mesh );
                std::vector<glm::mat4> skin;
                if ( entity.HasComponent<ECS::AnimationComponent>() )
                {
                    const auto& anim = entity.GetComponent<ECS::AnimationComponent>();
                    if ( anim.Animator )
                        skin = anim.Animator->GetPose().BoneMatrices;
                }
                if ( skin.empty() )
                    skin = BindSkinningMatrices( sk->GetSkeleton() );

                const Common::Math::AABB bounds = SkinnedLocalBounds( *sk, skin );
                float                    t      = 0.0f;
                if ( localRay.IntersectsAABB( bounds, t ) && t > 0.0f && t < closest )
                {
                    closest    = t;
                    bestXf     = xf;
                    bestAABB   = bounds;
                    bestLocal  = localRay;
                    bestLocalT = t;
                    bestUUID   = entity.GetComponent<ECS::UUIDComponent>().UUID;
                    hit        = true;
                }
            }
        }

        outHit.Hit = hit;
        if ( !hit )
            return false;

        outHit.Entity   = bestUUID;
        outHit.Distance = closest;
        outHit.Point    = ray.GetPoint( closest );

        // Box-face normal from the local hit (dominant axis of the offset from the AABB centre).
        const glm::vec3 lp = bestLocal.GetPoint( bestLocalT );
        const glm::vec3 c  = ( bestAABB.Min + bestAABB.Max ) * 0.5f;
        const glm::vec3 he = glm::max( ( bestAABB.Max - bestAABB.Min ) * 0.5f, glm::vec3( 1e-4f ) );
        const glm::vec3 dd = ( lp - c ) / he;
        const glm::vec3 ad = glm::abs( dd );
        const glm::vec3 ln = ( ad.x >= ad.y && ad.x >= ad.z ) ? glm::vec3( glm::sign( dd.x ), 0.0f, 0.0f )
                             : ( ad.y >= ad.z )                ? glm::vec3( 0.0f, glm::sign( dd.y ), 0.0f )
                                                               : glm::vec3( 0.0f, 0.0f, glm::sign( dd.z ) );
        outHit.Normal = glm::normalize( glm::mat3( bestXf ) * ln );
        return true;
    }
    Scene::Scene( std::string&& sceneName, Graphic::SceneRenderer* sceneRenderer )
         : m_SceneName( std::move( sceneName ) ), m_SceneRenderer( sceneRenderer )
    {
        SetupRegistryCallbacks();
    }

    NO_DISCARD Common::BoolResultStr Scene::BeginScene()
    {
        return m_SceneRenderer->BeginScene( *this );
    }

    NO_DISCARD Common::BoolResultStr Scene::Init()
    {
        m_SceneRenderer->Init();

        // Every scene gets a persistent EditorCamera as its Edit-mode view, so the viewport works
        // immediately (no "add a camera" requirement) and the editor view is independent of any scene
        // CameraComponent.
        if ( !m_EditorCamera )
            m_EditorCamera = std::make_shared<EditorCamera>();
        if ( m_State != SceneState::Play && !m_CameraPinned )
            SetActiveCamera( m_EditorCamera );

        return BOOLSUCCESS;
    }

    void Scene::PinActiveCamera( const std::shared_ptr<Core::Camera>& camera )
    {
        if ( !camera )
        {
            m_CameraPinned = false;
            return;
        }

        m_CameraPinned = true;
        SetActiveCamera( camera );

        // The scene still owns an EditorCamera (Init() always makes one). It polls the global mouse and
        // keyboard directly, so leaving it live in an offscreen scene means it flies along with the real
        // viewport — which is what made the Details preview follow the scene camera.
        if ( auto* editorCam = dynamic_cast<EditorCamera*>( m_EditorCamera.get() ) )
            editorCam->SetInputEnabled( false );
    }

    void Scene::UpdateActiveCameraSource()
    {
        // Camera source follows the play state: Edit/Paused -> EditorCamera; Play -> the main
        // CameraComponent (driven into a GameplayCamera each frame so moving the camera entity moves the
        // view). If Play has no camera entity, fall back to the editor camera so you still see the scene.
        if ( m_State == SceneState::Play )
        {
            const ECS::CameraComponent* mainCam    = nullptr;
            entt::entity                mainEntity = entt::null;
            auto camView = m_Registry.view<ECS::CameraComponent, ECS::TransformComponent>();
            for ( auto entity : camView )
            {
                const auto& cc = camView.get<ECS::CameraComponent>( entity );
                if ( !mainCam || cc.Data.IsMainCamera ) // prefer an IsMainCamera, else the first one
                {
                    mainCam    = &cc;
                    mainEntity = entity;
                    if ( cc.Data.IsMainCamera )
                        break;
                }
            }

            if ( mainCam && mainEntity != entt::null )
            {
                // WORLD transform of the camera entity (walk the parent chain), so a camera parented to a
                // moving entity (e.g. a child of the character controller) follows it — 1st/3rd person is
                // just the child's local offset.
                glm::mat4    world = m_Registry.get<ECS::TransformComponent>( mainEntity ).GetTransform();
                entt::entity cur   = mainEntity;
                while ( m_Registry.has<ECS::RelationshipComponent>( cur ) )
                {
                    const auto& rel = m_Registry.get<ECS::RelationshipComponent>( cur );
                    if ( rel.Parent == entt::null )
                        break;
                    cur = rel.Parent;
                    if ( m_Registry.has<ECS::TransformComponent>( cur ) )
                        world = m_Registry.get<ECS::TransformComponent>( cur ).GetTransform() * world;
                }
                const glm::vec3 worldPos   = glm::vec3( world[3] );
                const glm::vec3 worldEuler = glm::eulerAngles( glm::quat_cast( world ) );

                if ( !m_GameplayCamera )
                    m_GameplayCamera = std::make_shared<GameplayCamera>();
                static_cast<GameplayCamera*>( m_GameplayCamera.get() )
                     ->SetFromTransform( worldPos, worldEuler, mainCam->Data.FOV, mainCam->Data.Near,
                                         mainCam->Data.Far, m_ViewportWidth, m_ViewportHeight );
                if ( m_ActiveCamera != m_GameplayCamera )
                    SetActiveCamera( m_GameplayCamera );
            }
            else if ( m_ActiveCamera != m_EditorCamera )
            {
                SetActiveCamera( m_EditorCamera ); // no game camera -> keep the editor view
            }
        }
        else if ( m_EditorCamera && m_ActiveCamera != m_EditorCamera )
        {
            SetActiveCamera( m_EditorCamera );
        }
    }

    void Scene::OnUpdate( const Common::Timestep& ts )
    {
        // A pinned camera is driven from OUTSIDE the scene (the Details preview orbits its own), so the
        // play-state rule must not hand the view back to the EditorCamera behind its back — that is a
        // per-frame overwrite, and it is why the preview rendered through the input-driven editor camera
        // one frame after being told not to.
        if ( !m_CameraPinned )
            UpdateActiveCameraSource();

        Graphic::SceneRenderer::UpdateInfo sceneRendererInfo;
        sceneRendererInfo.Timestep = ts;

        // Gameplay time only advances in Play (Edit/Paused freeze it -> animation/physics/scripts hold).
        // Systems still RUN every frame (they collect render data); they just see a zero timestep when not
        // playing. The editor camera below uses the real ts so you can fly around while paused/editing.
        const Common::Timestep gameplayTs =
             ( m_State == SceneState::Play ) ? ts : Common::Timestep( 0.0f );

        // Push the active-camera snapshot to systems that lay out camera-relative geometry (billboarded
        // text). Done on the main thread before ExecuteSystems so the parallel system group reads it
        // race-free (SetCameraSnapshot is a no-op for every other system).
        if ( m_ActiveCamera )
        {
            const glm::mat4 camView = m_ActiveCamera->GetViewMatrix();
            const glm::vec3 camPos  = m_ActiveCamera->GetPosition();
            for ( auto& system : m_Systems )
                system->SetCameraSnapshot( camView, camPos );
        }

        {
            DESERT_PROFILE_SCOPE( "ECS Systems" );
            ExecuteSystems( gameplayTs );
        }

        // Dir lights
        {
            DESERT_PROFILE_SCOPE( "Scene: DirLights" );
            auto dirLightGroup =
                 m_Registry.group<ECS::DirectionLightComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each(
                 [&]( entt::entity entity, const auto& light, const auto& transform )
                 {
                     const glm::vec3& rawDir = transform.Translation;
                     if ( glm::length( rawDir ) > 0.001f )
                         sceneRendererInfo.DirLights.DirectionLights.push_back(
                              { glm::vec4( glm::normalize( rawDir ), 0.0f ),
                                glm::vec4( light.Data.Color, light.Data.Intensity ) } );
                 } );

            // The engine supports EXACTLY ONE directional light (DirectionLightsUB is a single
            // struct — a second payload overflows every PBR material's UB and aborts). Truncate
            // loudly instead of crashing; name the extras so the offending entity is findable.
            if ( sceneRendererInfo.DirLights.DirectionLights.size() > 1 )
            {
                std::string names;
                dirLightGroup.each(
                     [&]( entt::entity entity, const auto&, const auto& )
                     {
                         if ( m_Registry.has<ECS::TagComponent>( entity ) )
                             names += ( names.empty() ? "" : ", " ) +
                                      m_Registry.get<ECS::TagComponent>( entity ).Tag;
                     } );
                LOG_ERROR( "[Scene] {} directional lights collected ({}) — only ONE is supported; "
                           "using the first.",
                           sceneRendererInfo.DirLights.DirectionLights.size(), names );
                sceneRendererInfo.DirLights.DirectionLights.resize( 1 );
            }
        }

        // TODO: system
        const auto& mainCamera = m_MainCamera.lock();

        if ( mainCamera )
        {
            mainCamera->OnUpdate( ts );
        }

        {
            DESERT_PROFILE_SCOPE( "Scene: CmdBuffer ExecuteAll" );
            // Registration order — NOT completion order — so the frame's submission order is identical
            // to the old single-buffer sequential path.
            for ( const auto& buffer : m_SystemCommandBuffers )
                buffer->ExecuteAll( *m_SceneRenderer );
        }
        {
            DESERT_PROFILE_SCOPE( "Scene: CmdBuffer Clear" );
            for ( const auto& buffer : m_SystemCommandBuffers )
                buffer->Clear();
        }
        m_SceneRenderer->OnUpdate( std::move( sceneRendererInfo ) );
    }

    void Scene::ExecuteSystems( const Common::Timestep& gameplayTs )
    {
        // One command buffer per system, created on first use (AddSystem is a header template — the
        // buffers are built here where the type is complete).
        while ( m_SystemCommandBuffers.size() < m_Systems.size() )
            m_SystemCommandBuffers.emplace_back( std::make_unique<Graphic::Render::RenderCommandBuffer>() );

        const auto runOne = [&]( size_t index )
        {
            const auto& system = m_Systems[index];
            // Per-system timing (named by the system's type) so every ECS system is individually
            // visible in the profiler — no per-system edits.
            DESERT_PROFILE_SCOPE_DYNAMIC( typeid( *system ).name() );
            system->Update( m_Registry, *m_SystemCommandBuffers[index], gameplayTs );
        };

        // Sequential systems run in registration order; a maximal RUN of CanRunParallel() systems is
        // one parallel group (they have no cross-dependencies by contract — see System::CanRunParallel).
        // ParallelFor blocks until the group finishes, so the following sequential system still sees
        // every effect of the group — the schedule is semantically identical to the sequential loop.
        size_t i = 0;
        while ( i < m_Systems.size() )
        {
            if ( !m_Systems[i]->CanRunParallel() )
            {
                runOne( i );
                ++i;
                continue;
            }

            size_t groupEnd = i + 1;
            while ( groupEnd < m_Systems.size() && m_Systems[groupEnd]->CanRunParallel() )
                ++groupEnd;

            if ( const size_t count = groupEnd - i; count == 1 )
                runOne( i );
            else
                Common::JobSystem::Get().ParallelFor( count,
                                                      [&]( size_t local ) { runOne( i + local ); } );
            i = groupEnd;
        }
    }

    NO_DISCARD Common::BoolResultStr Scene::EndScene()
    {
        return m_SceneRenderer->EndScene();
    }

    NO_DISCARD const Graphic::Environment Scene::CreateEnvironment( const Common::Filepath& filepath )
    {
        return m_SceneRenderer->CreateEnvironment( filepath );
    }

    const std::optional<Graphic::Environment>& Scene::GetEnvironment() const
    {
        return m_SceneRenderer->GetEnvironment();
    }

    Desert::ECS::Entity& Scene::CreateNewEntity( std::string&& entityName )
    {
        const auto enttID = m_Registry.create();

        auto& entity = m_Entitys.emplace_back( enttID, m_Registry );

        entity.AddComponent<ECS::TagComponent>( std::move( entityName ) );
        entity.AddComponent<ECS::UUIDComponent>();
        entity.AddComponent<ECS::TransformComponent>();
        entity.AddComponent<ECS::RelationshipComponent>();

        m_EntitysMap[entity.GetComponent<ECS::UUIDComponent>().UUID] = (uint32_t)m_Entitys.size() - 1;

        return m_Entitys.back();
    }

    Desert::ECS::Entity& Scene::CreateEntityWithUUID( const Common::UUID& uuid, const std::string& name )
    {
        const auto enttID = m_Registry.create();

        auto& entity = m_Entitys.emplace_back( enttID, m_Registry );

        entity.AddComponent<ECS::TagComponent>( name );
        entity.AddComponent<ECS::UUIDComponent>( uuid );
        entity.AddComponent<ECS::TransformComponent>();
        entity.AddComponent<ECS::RelationshipComponent>();

        m_EntitysMap[uuid] = (uint32_t)m_Entitys.size() - 1;

        return m_Entitys.back();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> Scene::GetFinalImage() const
    {
        return m_SceneRenderer->GetFinalImage();
    }

    void Scene::Resize( const uint32_t width, const uint32_t height ) const
    {
        m_SceneRenderer->Resize( width, height );
        m_ViewportWidth  = width;
        m_ViewportHeight = height;

        const auto& mainCamera = m_MainCamera.lock();
        if ( mainCamera )
        {
            mainCamera->UpdateProjectionMatrix( width, height ); // TODO: Move to scene
        }
    }

    std::optional<std::reference_wrapper<const Desert::ECS::Entity>>
    Scene::FindEntityByID( const Common::UUID& uuid ) const
    {
        if ( auto it = m_EntitysMap.find( uuid ); it != m_EntitysMap.end() ) [[likely]]
        {
            return std::ref( m_Entitys.at( it->second ) );
        }
        else [[unlikely]]
        {
            return std::nullopt;
        }
    }

    void Scene::Serialize( const Assets::AssetManager* assetManager ) const
    {
        SceneSerializer serializer( this, assetManager );
        return serializer.SaveToFile();
    }

    void Scene::RegisterExternalPass( Graphic::ExternalPassSpecification&& spec )
    {
        m_SceneRenderer->RegisterExternalPass( std::move( spec ) );
    }

    void Scene::UnregisterExternalPass( const std::string& name )
    {
        m_SceneRenderer->UnregisterExternalPass( name );
    }

    void Scene::OnEntityCreated_Camera()
    {
        // Intentionally a no-op now: a scene CameraComponent is a GAME camera, NOT the editor view. The
        // editor renders through its own EditorCamera (set via SetActiveCamera); Play mode switches to the
        // main CameraComponent via a GameplayCamera. (FindMainCamera kept for the Play-mode lookup.)
    }

    void Scene::FindMainCamera()
    {
        m_MainCamera.reset();

        auto cameraView = m_Registry.view<ECS::CameraComponent>();

        for ( auto entity : cameraView )
        {
            auto& cameraComponent = cameraView.get<ECS::CameraComponent>( entity );

            if ( cameraComponent.Data.IsMainCamera )
            {
                // TODO: Get from scene config
                // Through the engine's projection factory so this camera shares the reversed-Z convention
                // with every other one (Core/Projection.hpp). Near/far are the Camera defaults in world
                // units — the literal 0.1/1000 that stood here were metres-era leftovers, i.e. a 1 mm near
                // plane and a 10 m far plane once a unit became a centimetre.
                const glm::mat4 projection = MakePerspective( glm::radians( 45.0f ), 1280.0f / 720.0f,
                                                              kDefaultNearPlane, kDefaultFarPlane );
                cameraComponent.Camera = std::make_shared<EditorCamera>( projection );
                m_MainCamera           = cameraComponent.Camera;

                break;
            }
        }

        if ( !cameraView.empty() )
        {
            auto  entity          = *cameraView.begin();
            auto& cameraComponent = cameraView.get<ECS::CameraComponent>( entity );
            m_MainCamera          = cameraComponent.Camera;

            cameraComponent.Data.IsMainCamera = true;
        }
    }

    const std::shared_ptr<Desert::Graphic::Framebuffer> Scene::GetTargetFramebuffer() const
    {
        return m_SceneRenderer->GetTargetFramebuffer();
    }

    void Scene::Clear()
    {
        m_Registry.clear();

        m_Entitys.clear();
        m_EntitysMap.clear();

        SetupRegistryCallbacks();
    }

    void Scene::SetupRegistryCallbacks()
    {
        // Disconnect first — Clear() calls this on every scene reload, and entt keeps signal connections
        // across registry.clear(), so re-connecting without this accumulates duplicate handler invocations.
        m_Registry.on_construct<ECS::CameraComponent>().disconnect( this );
        m_Registry.on_construct<ECS::CameraComponent>().connect<&Scene::OnEntityCreated_Camera>( this );
    }

    void Scene::Attach( ECS::Entity parent, ECS::Entity child )
    {
        if ( !parent || !child || parent.GetHandle() == child.GetHandle() )
            return;

        // Refuse to attach an entity to its own descendant — that would make the hierarchy a cycle
        // (every parent-chain walk in the engine would spin forever).
        for ( entt::entity cur = parent.GetHandle(); cur != entt::null;
              cur = m_Registry.has<ECS::RelationshipComponent>( cur )
                        ? m_Registry.get<ECS::RelationshipComponent>( cur ).Parent
                        : entt::null )
        {
            if ( cur == child.GetHandle() )
                return;
        }

        auto& parentRel = parent.GetComponent<ECS::RelationshipComponent>();
        auto& childRel  = child.GetComponent<ECS::RelationshipComponent>();

        if ( childRel.Parent == parent.GetHandle() )
            return;
        if ( childRel.Parent != entt::null )
            Detach( child ); // reparent: without this the old parent kept a stale Children entry

        childRel.Parent = parent.GetHandle();
        parentRel.Children.push_back( child.GetHandle() );
    }

    void Scene::Detach( ECS::Entity child )
    {
        if ( !child || !child.HasComponent<ECS::RelationshipComponent>() )
            return;

        auto& childRel = child.GetComponent<ECS::RelationshipComponent>();
        if ( childRel.Parent == entt::null )
            return;

        if ( m_Registry.valid( childRel.Parent ) && m_Registry.has<ECS::RelationshipComponent>( childRel.Parent ) )
        {
            auto& siblings = m_Registry.get<ECS::RelationshipComponent>( childRel.Parent ).Children;
            siblings.erase( std::remove( siblings.begin(), siblings.end(), child.GetHandle() ),
                            siblings.end() );
        }
        childRel.Parent = entt::null;
    }

    void Scene::SetVisibleRecursive( ECS::Entity entity, bool visible )
    {
        if ( !entity ) return;

        if ( entity.HasComponent<ECS::VisibilityComponent>() )
            entity.GetComponent<ECS::VisibilityComponent>().Visible = visible;
        else
            entity.AddComponent<ECS::VisibilityComponent>().Visible = visible;

        if ( entity.HasComponent<ECS::RelationshipComponent>() )
        {
            auto& rel = entity.GetComponent<ECS::RelationshipComponent>();
            for ( auto childHandle : rel.Children )
                SetVisibleRecursive( ECS::Entity( childHandle, m_Registry ), visible );
        }
    }

    void Scene::DestroyEntity( ECS::Entity entity )
    {
        if ( !entity ) return;
        
        auto uuid = entity.GetComponent<ECS::UUIDComponent>().UUID;
        
        // Destroy children
        if ( entity.HasComponent<ECS::RelationshipComponent>() )
        {
            auto& rel = entity.GetComponent<ECS::RelationshipComponent>();
            auto childrenCopy = rel.Children; // copy to avoid issues during iteration
            for ( auto childHandle : childrenCopy )
            {
                DestroyEntity( ECS::Entity( childHandle, m_Registry ) );
            }
        }

        // Remove from parent
        if ( entity.HasComponent<ECS::RelationshipComponent>() )
        {
            auto& rel = entity.GetComponent<ECS::RelationshipComponent>();
            if ( rel.Parent != entt::null )
            {
                ECS::Entity parent = ECS::Entity( rel.Parent, m_Registry );
                if ( parent )
                {
                    auto& parentRel = parent.GetComponent<ECS::RelationshipComponent>();
                    auto it = std::find( parentRel.Children.begin(), parentRel.Children.end(), entity.GetHandle() );
                    if ( it != parentRel.Children.end() )
                        parentRel.Children.erase( it );
                }
            }
        }

        m_Registry.destroy( entity.GetHandle() );

        auto it = std::find_if( m_Entitys.begin(), m_Entitys.end(), [&]( const ECS::Entity& e ) { return e.GetHandle() == entity.GetHandle(); } );
        if ( it != m_Entitys.end() )
        {
            // m_EntitysMap stores INDICES into m_Entitys — erasing from the middle shifts every entity
            // after the erased one, so those stored indices must shift too (otherwise FindEntityByID
            // silently returns the WRONG entity for every UUID registered after the deleted one).
            const size_t removedIndex = static_cast<size_t>( it - m_Entitys.begin() );
            m_Entitys.erase( it );
            for ( auto& [id, index] : m_EntitysMap )
                if ( index > removedIndex )
                    --index;
        }

        m_EntitysMap.erase( uuid );
    }

} // namespace Desert::Core