#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>

#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/System/MeshRenderSystem.hpp>
#include <Engine/ECS/System/SkyboxRenderSystem.hpp>

#include <Engine/Core/Serialize/SceneSerializer.hpp>

namespace Desert::Core
{
    enum SceneSystem
    {
        MeshRenderer   = 0,
        SkyboxRenderer = 1,
    };

    Scene::Scene( std::string&&                                           sceneName,
                  const std::shared_ptr<Runtime::RuntimeResourceManager>& resourceManager )
         : m_SceneName( std::move( sceneName ) ), m_SceneRenderer( std::make_shared<Graphic::SceneRenderer>() ),
           m_ResourceResolver( std::make_shared<Runtime::ResourceResolver>( resourceManager ) ), m_Systems()
    {

        RegisterSystem<ECS::MeshRenderSystem>( SceneSystem::MeshRenderer, m_SceneRenderer, m_ResourceResolver );
        RegisterSystem<ECS::SkyboxRenderSystem>( SceneSystem::SkyboxRenderer, m_SceneRenderer,
                                                 m_ResourceResolver );
    }

    NO_DISCARD Common::BoolResult Scene::BeginScene( const Core::Camera& camera )
    {
        return m_SceneRenderer->BeginScene( shared_from_this(), camera );
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

    const Desert::Graphic::Environment& Scene::GetEnvironment() const
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

} // namespace Desert::Core