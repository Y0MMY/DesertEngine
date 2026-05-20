#include <Engine/Core/Serialize/SceneSerializer.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <Common/Core/Constants.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

#include <regex>

#include "GLMReflect.hpp"

namespace Desert::Core
{

    namespace internal
    {
        struct TagComponentSer
        {
            std::string Tag;
        };

        struct UUIDComponentSer
        {
            std::string UUID;
        };

        struct StaticMeshComponentSer
        {
            Common::Filepath MeshAssetPath;
        };

        struct TransformComponentSer
        {
            glm::vec3 Position;
            glm::vec3 Rotation;
            glm::vec3 Scale;
        };

        struct MaterialComponentSer
        {
            std::string MaterialAssetPath;
        };

        struct SkyboxComponentSer
        {
            Common::Filepath Filepath;
        };

        struct CameraComponentSer
        {
            bool IsMainCamera = false;
        };

        struct EntitySer
        {
            std::optional<TagComponentSer>        Tag;
            std::optional<UUIDComponentSer>       UUID;
            std::optional<TransformComponentSer>  Transform;
            std::optional<StaticMeshComponentSer> StaticMesh;
            std::optional<MaterialComponentSer>   Material;
            std::optional<SkyboxComponentSer>     Skybox;
            std::optional<CameraComponentSer>     Camera;
            bool                                  HasDirectionLight = false;
        };

        struct SceneSerialized
        {
            std::string            SceneName;
            std::vector<EntitySer> Entities;
        };
    } // namespace internal

    SceneSerializer::SceneSerializer( const Scene* scene, const Assets::AssetManager* assetManager )
         : m_Scene( (Scene*)scene ), m_AssetManager( (Assets::AssetManager*)assetManager )
    {
    }

    std::string SceneSerializer::SerializeToJson() const
    {
        internal::SceneSerialized scene;
        scene.SceneName = m_Scene->GetSceneName();

        for ( const auto& entity : m_Scene->GetAllEntities() )
        {
            internal::EntitySer entitySer;

            // Tag
            if ( entity.HasComponent<ECS::TagComponent>() )
            {
                const auto& tag = entity.GetComponent<ECS::TagComponent>();
                entitySer.Tag   = internal::TagComponentSer{ tag.Tag };
            }

            // UUID
            if ( entity.HasComponent<ECS::UUIDComponent>() )
            {
                const auto& uuid = entity.GetComponent<ECS::UUIDComponent>();
                entitySer.UUID   = internal::UUIDComponentSer{ uuid.UUID.ToString() };
            }

            // Transform
            if ( entity.HasComponent<ECS::TransformComponent>() )
            {
                const auto& transform = entity.GetComponent<ECS::TransformComponent>();

                entitySer.Transform =
                     internal::TransformComponentSer{ transform.Translation, transform.Rotation, transform.Scale };
            }

            // Static Mesh
            if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                const auto& meshComponent = entity.GetComponent<ECS::StaticMeshComponent>();

               // if ( meshComponent.GetMeshType() == ECS::StaticMeshComponent::Type::Asset )
                {
                    /*const auto& meshAsset =
                         m_AssetManager->FindByHandle<Assets::MeshAsset>( meshComponent.MeshHandle.value() );

                    entitySer.StaticMesh =
                         internal::StaticMeshComponentSer{ meshAsset ? meshAsset->GetMetadata().Filepath : "" };*/
                }
            }

            //// Material
            // if ( entity.HasComponent<ECS::MaterialComponent>() )
            //{
            //     const auto& materialComponent = entity.GetComponent<ECS::MaterialComponent>();

            //    const auto& materialAsset =
            //         m_AssetManager->FindByHandle<Assets::MaterialAsset>( materialComponent.MaterialHandle );

            //    entitySer.Material =
            //         internal::MaterialComponentSer{ materialAsset ? materialAsset->GetFilepath().string() : ""
            //         };
            //}

            // Skybox
            if ( entity.HasComponent<ECS::SkyboxComponent>() )
            {
                const auto& skybox      = entity.GetComponent<ECS::SkyboxComponent>();
                const auto& skyboxAsset = m_AssetManager->FindByHandle<Assets::SkyboxAsset>( skybox.SkyboxHandle );
                entitySer.Skybox        = internal::SkyboxComponentSer{
                     skyboxAsset ? skyboxAsset->GetMetadata().Filepath.string() : "" };
            }

            // Camera
            if ( entity.HasComponent<ECS::CameraComponent>() )
            {
                const auto& cam = entity.GetComponent<ECS::CameraComponent>();

                entitySer.Camera = internal::CameraComponentSer{ cam.IsMainCamera };
            }

            // Direction light
            entitySer.HasDirectionLight = entity.HasComponent<ECS::DirectionLightComponent>();

            scene.Entities.push_back( std::move( entitySer ) );
        }

        return rfl::json::write( scene );
    }

    void SceneSerializer::DeserializeFromJson( const std::string& json ) const
    {
        auto sceneData = rfl::json::read<internal::SceneSerialized>( json );

        if ( !sceneData )
            return;

        m_Scene->SetSceneName( sceneData->SceneName );

        for ( const auto& entitySer : sceneData->Entities )
        {
            Common::UUID uuid;
            std::string  tag = "Entity";

            // UUID
            if ( entitySer.UUID.has_value() )
            {
                uuid = Common::UUID( entitySer.UUID->UUID );
            }
            else
            {
                uuid = Common::UUID();
            }

            // Tag
            if ( entitySer.Tag.has_value() )
            {
                tag = entitySer.Tag->Tag;
            }

            ECS::Entity entity = m_Scene->CreateEntityWithUUID( uuid, tag );

            // Transform
            if ( entitySer.Transform.has_value() )
            {
                auto& tc       = entity.GetComponent<ECS::TransformComponent>();
                tc.Translation = entitySer.Transform->Position;
                tc.Rotation    = entitySer.Transform->Rotation;
                tc.Scale       = entitySer.Transform->Scale;
            }

            // Static Mesh
            if ( entitySer.StaticMesh.has_value() )
            {
                const auto& path = entitySer.StaticMesh->MeshAssetPath;

                if ( !path.empty() )
                {
                   /* auto meshAsset =
                         m_AssetManager->CreateAsset<Assets::MeshAsset>( Assets::AssetPriority::Low, path );

                    if ( meshAsset )
                    {
                        auto& smc      = entity.AddComponent<ECS::StaticMeshComponent>();
                        smc.MeshHandle = meshAsset->GetMetadata().Handle;
                    }*/
                }
            }

            if ( entitySer.Camera.has_value() )
            {
                auto& cam        = entity.AddComponent<ECS::CameraComponent>();
                cam.IsMainCamera = entitySer.Camera->IsMainCamera;
            }

            // Skybox
            if ( entitySer.Skybox.has_value() )
            {
                const auto& path = entitySer.Skybox->Filepath;

                if ( !path.empty() )
                {
                   /* auto skyboxAsset =
                         m_AssetManager->CreateAsset<Assets::MeshAsset>( Assets::AssetPriority::Low, path );

                    if ( skyboxAsset )
                    {
                        auto& skybox        = entity.AddComponent<ECS::SkyboxComponent>();
                        skybox.SkyboxHandle = skyboxAsset->GetMetadata().Handle;
                    }*/
                }
            }

            // Direction Light
            if ( entitySer.HasDirectionLight )
            {
                entity.AddComponent<ECS::DirectionLightComponent>();
            }

            if ( entitySer.HasDirectionLight )
            {
                entity.AddComponent<ECS::DirectionLightComponent>();
            }
        }
    }

    void SceneSerializer::SaveToFile() const
    {
        const auto& serialized = SerializeToJson();
        auto        sceneName  = std::regex_replace( m_Scene->GetSceneName(), std::regex( "\\s+" ), "_" );
        sceneName += Common::Constants::Extensions::SCENE_EXTENSION;
        const Common::Filepath pathToSave = Common::Constants::Path::SCENE_PATH / sceneName;
        Common::Utils::FileSystem::WriteContentToFile( pathToSave, serialized );
    }

} // namespace Desert::Core