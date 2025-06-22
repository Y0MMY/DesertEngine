#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/ECS/Entity.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName )
         : m_SceneName( sceneName ), m_SceneRenderer( std::make_unique<Graphic::SceneRenderer>() )
    {
    }

    NO_DISCARD Common::BoolResult Scene::BeginScene( const Core::Camera& camera )
    {
        return m_SceneRenderer->BeginScene( shared_from_this(), camera );
    }

    NO_DISCARD Common::BoolResult Scene::Init()
    {
        return m_SceneRenderer->Init();
    }

    void Scene::SetEnvironment( const Graphic::Environment& environment )
    {
        m_SceneRenderer->SetEnvironment( environment );
    }

    void Scene::AddMeshToRenderList( const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform ) const
    {
        m_SceneRenderer->AddToRenderMeshList( mesh, transform );
    }

    void Scene::OnUpdate( const Common::Timestep& ts )
    {
        Graphic::DTO::SceneRendererUpdate sceneRendererInfo;
        sceneRendererInfo.Timestep = ts;

        // Skybox
        {
            const auto& skyboxes = m_Registry.view<ECS::SkyboxComponent>();
            for ( const auto skyboxEntity : skyboxes )
            {
                const auto& skybox = m_Registry.get<ECS::SkyboxComponent>( skyboxEntity );
                SetEnvironment( skybox.Env );
            }
        }

        // Dir lights
        {
            auto dirLightGroup =
                 m_Registry.group<ECS::DirectionLightComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each( [&]( const auto& light, const auto& transform )
                                { sceneRendererInfo.DirLights.push_back( { transform.Position } ); } );
        }

        // Mesh
        {
            auto dirLightGroup = m_Registry.group<ECS::StaticMeshComponent>( entt::get<ECS::TransformComponent> );

            dirLightGroup.each(
                 [&]( const auto& staticMesh, const auto& transform )
                 {
                     AddMeshToRenderList( staticMesh.AssetMesh ? staticMesh.AssetMesh->GetMesh() : nullptr,
                                          transform.GetTransform() );
                 } );
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

} // namespace Desert::Core