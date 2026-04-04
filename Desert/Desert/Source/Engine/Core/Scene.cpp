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
                 { sceneRendererInfo.DirLights.DirectionLights.push_back( { transform.Translation } ); } );
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

        m_EntitysMap[entity.GetComponent<ECS::UUIDComponent>().UUID] = m_Entitys.size() - 1;

        return entity;
    }

    Desert::ECS::Entity& Scene::CreateEntityWithUUID( const Common::UUID& uuid, const std::string& name )
    {
        const auto enttID = m_Registry.create();

        auto& entity = m_Entitys.emplace_back( enttID, m_Registry );

        entity.AddComponent<ECS::TagComponent>( name );
        entity.AddComponent<ECS::UUIDComponent>( uuid );
        entity.AddComponent<ECS::TransformComponent>();

        m_EntitysMap[uuid] = m_Entitys.size() - 1;

        return entity;
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

} // namespace Desert::Core