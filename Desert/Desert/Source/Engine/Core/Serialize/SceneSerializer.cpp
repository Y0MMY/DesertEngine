#include <Engine/Core/Serialize/SceneSerializer.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/MaterialAsset.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <Common/Core/Constants.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

#include <regex>

namespace rfl
{
    template <>
    struct Reflector<glm::vec3>
    {
        struct ReflType
        {
            float x;
            float y;
            float z;
        };

        static glm::vec3 to( const ReflType& v ) noexcept
        {
            return { v.x, v.y, v.z };
        }

        static ReflType from( const glm::vec3& v ) noexcept
        {
            return { v.x, v.y, v.z };
        }
    };
} // namespace rfl

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
            std::string MeshAssetPath;
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
            std::string Filepath;
        };

        struct EntitySer
        {
            std::optional<TagComponentSer>        Tag;
            std::optional<UUIDComponentSer>       UUID;
            std::optional<TransformComponentSer>  Transform;
            std::optional<StaticMeshComponentSer> StaticMesh;
            std::optional<MaterialComponentSer>   Material;
            std::optional<SkyboxComponentSer>     Skybox;
            bool                                  HasDirectionLight = false;
        };

        struct SceneSerialized
        {
            std::string            SceneName;
            std::vector<EntitySer> Entities;
        };
    } // namespace internal

    SceneSerializer::SceneSerializer( const Scene*                                 scene,
                                      const std::shared_ptr<Assets::AssetManager>& assetManager )
         : m_Scene( (Scene*)scene ), m_AssetManager( assetManager )
    {
    }

    std::string SceneSerializer::SerializeToJson() const
    {
        internal::SceneSerialized scene;
        scene.SceneName = m_Scene->GetSceneName();

        for ( const auto& entity : m_Scene->GetAllEntities() )
        {
            internal::EntitySer entitySer;

            if ( entity.HasComponent<ECS::TagComponent>() )
            {
                auto& tag     = entity.GetComponent<ECS::TagComponent>();
                entitySer.Tag = internal::TagComponentSer{ tag.Tag };
            }

            if ( entity.HasComponent<ECS::UUIDComponent>() )
            {
                auto& uuid     = entity.GetComponent<ECS::UUIDComponent>();
                entitySer.UUID = internal::UUIDComponentSer{ uuid.UUID.ToString() };
            }

            if ( entity.HasComponent<ECS::TransformComponent>() )
            {
                auto& transform = entity.GetComponent<ECS::TransformComponent>();
                entitySer.Transform =
                     internal::TransformComponentSer{ transform.Position, transform.Rotation, transform.Scale };
            }

           /* if ( entity.HasComponent<ECS::StaticMeshComponent>() )
            {
                auto&       meshComponent = entity.GetComponent<ECS::StaticMeshComponent>();
                const auto& mesh = m_AssetManager->FindByHandle<Assets::MeshAsset>( meshComponent.MeshHandle );
                entitySer.StaticMesh =
                     internal::StaticMeshComponentSer{ mesh ? mesh->GetBaseFilepath().string() : "" };
            }*/

           /* if ( entity.HasComponent<ECS::MaterialComponent>() )
            {
                auto&       materialhComponent = entity.GetComponent<ECS::MaterialComponent>();
                const auto& material =
                     m_AssetManager->FindByHandle<Assets::MeshAsset>( materialhComponent.MaterialHandle );
                entitySer.Material =
                     internal::MaterialComponentSer{ material ? material->GetFilepath().string() : "" };
            }*/

            /*if ( entity.HasComponent<ECS::SkyboxComponent>() )
            {
                auto& skybox     = entity.GetComponent<ECS::SkyboxComponent>();
                entitySer.Skybox = internal::SkyboxComponentSer{ skybox.Filepath.string() };
            }*/

            entitySer.HasDirectionLight = entity.HasComponent<ECS::DirectionLightComponent>();

            scene.Entities.push_back( std::move( entitySer ) );
        }

        // Convert to JSON
        return rfl::json::write(scene);
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