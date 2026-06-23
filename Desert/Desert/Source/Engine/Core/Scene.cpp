#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

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
        return BOOLSUCCESS;
    }

    void Scene::OnUpdate( const Common::Timestep& ts )
    {
        Graphic::SceneRenderer::UpdateInfo sceneRendererInfo;
        sceneRendererInfo.Timestep = ts;

        std::ranges::for_each( m_Systems,
                               [&]( const auto& system ) { system->Update( m_Registry, *m_CommandBuffer, ts ); } );

        // Dir lights
        {
            auto dirLightGroup =
                 m_Registry.group<ECS::DirectionLightComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each(
                 [&]( const auto& light, const auto& transform )
                 {
                     const glm::vec3& rawDir = transform.Translation;
                     if ( glm::length( rawDir ) > 0.001f )
                         sceneRendererInfo.DirLights.DirectionLights.push_back( { glm::normalize( rawDir ) } );
                 } );
        }

        // TODO: system
        const auto& mainCamera = m_MainCamera.lock();

        if ( mainCamera )
        {
            mainCamera->OnUpdate( ts );
        }

        m_CommandBuffer->ExecuteAll( *m_SceneRenderer );
        m_CommandBuffer->Clear();
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
        FindMainCamera();
    }

    void Scene::FindMainCamera()
    {
        m_MainCamera.reset();

        auto cameraView = m_Registry.view<ECS::CameraComponent>();

        for ( auto entity : cameraView )
        {
            auto& cameraComponent = cameraView.get<ECS::CameraComponent>( entity );

            if ( cameraComponent.IsMainCamera )
            {
                // TODO: Get from scene config
                const glm::mat4 projection =
                     glm::perspectiveFov( glm::radians( 45.0f ), 1280.0f, 720.0f, 0.1f, 1000.0f );
                cameraComponent.Camera = std::make_shared<Core::Camera>( projection );
                m_MainCamera           = cameraComponent.Camera;

                break;
            }
        }

        if ( !cameraView.empty() )
        {
            auto  entity          = *cameraView.begin();
            auto& cameraComponent = cameraView.get<ECS::CameraComponent>( entity );
            m_MainCamera          = cameraComponent.Camera;

            cameraComponent.IsMainCamera = true;
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