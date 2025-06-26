#include <Engine/Core/Scene.hpp>

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/ECS/Entity.hpp>

#include <Engine/Core/Serialize/SceneSerializer.hpp>

namespace Desert::Core
{

    Scene::Scene( const std::string& sceneName, const std::shared_ptr<Assets::AssetManager>& assetManager )
         : m_SceneName( sceneName ), m_SceneRenderer( std::make_unique<Graphic::SceneRenderer>() ),
           m_AssetManager( assetManager )
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

    void Scene::AddMeshToRenderList( const Assets::AssetHandle                   handle,
                                     const Assets::Asset<Assets::MaterialAsset>& material,
                                     const glm::mat4&                            transform ) const
    {
        const auto& assetMesh = m_AssetManager->FindByHandle<Assets::MeshAsset>( handle );
        m_SceneRenderer->AddToRenderMeshList( assetMesh ? assetMesh->GetMesh() : nullptr, nullptr /*material*/,  transform );
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
            auto meshView = m_Registry.view<ECS::StaticMeshComponent, ECS::TransformComponent>();
            meshView.each(
                 [&]( const auto entity, const auto& staticMesh, const auto& transform )
                 {
                     if ( m_Registry.has<ECS::MaterialComponent>( entity ) )
                     {
                         const auto& material = m_Registry.get<ECS::MaterialComponent>( entity );
                         AddMeshToRenderList( staticMesh.MeshHandle, nullptr, transform.GetTransform() );
                     }
                     else
                     {
                         AddMeshToRenderList( staticMesh.MeshHandle, nullptr, transform.GetTransform() );
                     }
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
        SceneSerializer serializer( this, m_AssetManager );
        return serializer.SaveToFile();
    }

} // namespace Desert::Core