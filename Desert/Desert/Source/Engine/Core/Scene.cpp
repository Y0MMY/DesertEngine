#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Common/Core/Profiler.hpp>

#include <typeinfo>

#include <Engine/Core/Serialize/SceneSerializer.hpp>

namespace Desert::Core
{
    Scene::Scene( std::string&& sceneName, Graphic::SceneRenderer* sceneRenderer )
         : m_SceneName( std::move( sceneName ) ), m_SceneRenderer( sceneRenderer )
    {
        SetupRegistryCallbacks();
        m_CommandBuffer = std::make_unique<Graphic::Render::RenderCommandBuffer>();
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
        if ( m_State != SceneState::Play )
            SetActiveCamera( m_EditorCamera );

        return BOOLSUCCESS;
    }

    void Scene::OnUpdate( const Common::Timestep& ts )
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

        Graphic::SceneRenderer::UpdateInfo sceneRendererInfo;
        sceneRendererInfo.Timestep = ts;

        // Gameplay time only advances in Play (Edit/Paused freeze it -> animation/physics/scripts hold).
        // Systems still RUN every frame (they collect render data); they just see a zero timestep when not
        // playing. The editor camera below uses the real ts so you can fly around while paused/editing.
        const Common::Timestep gameplayTs =
             ( m_State == SceneState::Play ) ? ts : Common::Timestep( 0.0f );

        {
            DESERT_PROFILE_SCOPE( "ECS Systems" );
            std::ranges::for_each( m_Systems,
                                   [&]( const auto& system )
                                   {
                                       // Per-system timing (named by the system's type) so every ECS system
                                       // is individually visible in the profiler — no per-system edits.
                                       DESERT_PROFILE_SCOPE_DYNAMIC( typeid( *system ).name() );
                                       system->Update( m_Registry, *m_CommandBuffer, gameplayTs );
                                   } );
        }

        // Dir lights
        {
            DESERT_PROFILE_SCOPE( "Scene: DirLights" );
            auto dirLightGroup =
                 m_Registry.group<ECS::DirectionLightComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each(
                 [&]( const auto& light, const auto& transform )
                 {
                     const glm::vec3& rawDir = transform.Translation;
                     if ( glm::length( rawDir ) > 0.001f )
                         sceneRendererInfo.DirLights.DirectionLights.push_back(
                              { glm::vec4( glm::normalize( rawDir ), 0.0f ),
                                glm::vec4( light.Data.Color, light.Data.Intensity ) } );
                 } );
        }

        // TODO: system
        const auto& mainCamera = m_MainCamera.lock();

        if ( mainCamera )
        {
            mainCamera->OnUpdate( ts );
        }

        {
            DESERT_PROFILE_SCOPE( "Scene: CmdBuffer ExecuteAll" );
            m_CommandBuffer->ExecuteAll( *m_SceneRenderer );
        }
        {
            DESERT_PROFILE_SCOPE( "Scene: CmdBuffer Clear" );
            m_CommandBuffer->Clear();
        }
        m_SceneRenderer->OnUpdate( std::move( sceneRendererInfo ) );
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

    void Scene::RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                      std::shared_ptr<Graphic::RenderPass>&& renderPass )
    {
        m_SceneRenderer->RegisterExternalPass( std::move( name ), execute, std::move( renderPass ) );
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
                const glm::mat4 projection =
                     glm::perspectiveFov( glm::radians( 45.0f ), 1280.0f, 720.0f, 0.1f, 1000.0f );
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
        auto& parentRel = parent.GetComponent<ECS::RelationshipComponent>();
        auto& childRel  = child.GetComponent<ECS::RelationshipComponent>();

        if ( childRel.Parent != entt::null )
        {
            //  Detach( child );
        }

        childRel.Parent = parent.GetHandle();
        parentRel.Children.push_back( child.GetHandle() );
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
            m_Entitys.erase( it );
        }

        m_EntitysMap.erase( uuid );
    }

} // namespace Desert::Core