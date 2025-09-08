#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/System/MeshRenderSystem.hpp>
#include <Engine/ECS/System/SkyboxRenderSystem.hpp>
#include <Engine/ECS/System/PointLightSystem.hpp>

#include <Engine/Core/Serialize/SceneSerializer.hpp>

namespace Desert::Core
{
    enum SceneSystem
    {
        MeshRenderer   = 0,
        SkyboxRenderer = 1,
        PointLight     = 2, //TODO: add direction light
    };

    Scene::Scene( std::string&& sceneName, const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry )
         : m_SceneName( std::move( sceneName ) ), m_SceneRenderer( std::make_shared<Graphic::SceneRenderer>() ),
           m_ResourceRegistry( resourceRegistry ), m_Systems()
    {

        RegisterSystem<ECS::MeshRenderSystem>( SceneSystem::MeshRenderer, m_SceneRenderer, m_ResourceRegistry );
        RegisterSystem<ECS::SkyboxRenderSystem>( SceneSystem::SkyboxRenderer, m_SceneRenderer,
                                                 m_ResourceRegistry );
        RegisterSystem<ECS::PointLightRenderSystem>( SceneSystem::PointLight, m_SceneRenderer,
                                                     m_ResourceRegistry );

        m_Registry.on_construct<ECS::CameraComponent>().connect<&Scene::OnEntityCreated_Camera>( this );
    }

    NO_DISCARD Common::BoolResult Scene::BeginScene()
    {
        return m_SceneRenderer->BeginScene( shared_from_this(), m_MainCamera );
    }

    NO_DISCARD Common::BoolResult Scene::Init()
    {
        return m_SceneRenderer->Init();
    }

    void Scene::OnUpdate( const Common::Timestep& ts )
    {
        Graphic::SceneRendererUpdate sceneRendererInfo;
        sceneRendererInfo.Timestep = ts;

        std::for_each( m_Systems.begin(), m_Systems.end(),
                       [&]( const auto& system ) { system->Update( m_Registry, ts ); } );

        // Dir lights
        {
            auto dirLightGroup =
                 m_Registry.group<ECS::DirectionLightComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each( [&]( const auto& light, const auto& transform )
                                { sceneRendererInfo.DirLights.push_back( { transform.Translation } ); } );
        }

        // TODO: system
        if ( m_MainCamera )
        {
            m_MainCamera->OnUpdate( ts );
        }

        m_SceneRenderer->OnUpdate( std::move( sceneRendererInfo ) );
    }

    NO_DISCARD Common::BoolResult Scene::EndScene()
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
        auto&      entity = m_Entitys.emplace_back( std::move( entityName ), enttID, this );
        m_EntitysMap[entity.GetComponent<ECS::UUIDComponent>().UUID] = m_Entitys.size() - 1;

        return entity;
    }

    const std::shared_ptr<Desert::Graphic::Image2D> Scene::GetFinalImage() const
    {
        return m_SceneRenderer->GetFinalImage();
    }

    void Scene::Shutdown()
    {
        m_SceneRenderer->Shutdown();
        m_SceneRenderer.reset();
    }

    void Scene::Resize( const uint32_t width, const uint32_t height ) const
    {
        m_SceneRenderer->Resize( width, height );
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

    void Scene::Serialize() const
    {
        //   SceneSerializer serializer( this, m_AssetManager );
        // return serializer.SaveToFile();
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

        if ( !m_MainCamera && !cameraView.empty() )
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

} // namespace Desert::Core