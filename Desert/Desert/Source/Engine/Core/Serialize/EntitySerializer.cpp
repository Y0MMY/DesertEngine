#include "EntitySerializer.hpp"
#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Assets/Prefab/PrefabAsset.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Core::Serialize
{
    Assets::EntityData EntitySerializer::SerializeEntity( ECS::Entity entity, const Assets::AssetManager& assetManager )
    {
        Assets::EntityData data;
        
        // Basic Info
        if ( entity.HasComponent<ECS::UUIDComponent>() )
            data.id = entity.GetComponent<ECS::UUIDComponent>().UUID;
        else
            data.id = Common::UUID();

        if ( entity.HasComponent<ECS::RelationshipComponent>() )
        {
            entt::entity parentHandle = entity.GetComponent<ECS::RelationshipComponent>().Parent;
            if ( parentHandle != entt::null )
            {
                ECS::Entity parentEntity{ parentHandle, *entity.GetRegistry() };
                if ( parentEntity.HasComponent<ECS::UUIDComponent>() )
                {
                    data.parent = parentEntity.GetComponent<ECS::UUIDComponent>().UUID;
                }
            }
        }

        if ( entity.HasComponent<ECS::TagComponent>() )
            data.Tag = entity.GetComponent<ECS::TagComponent>().Tag;

        if ( entity.HasComponent<ECS::PrefabComponent>() )
        {
            auto handle = entity.GetComponent<ECS::PrefabComponent>().Prefab;
            if ( handle )
            {
                auto asset = assetManager.FindByHandle<Assets::PrefabAsset>( handle );
                if ( asset )
                {
                    data.PrefabPath = asset->GetMetadata().Filepath.string();
                }
            }
        }

        // Transform
        if ( entity.HasComponent<ECS::TransformComponent>() )
        {
            const auto& tc = entity.GetComponent<ECS::TransformComponent>();
            data.Translation = tc.Translation;
            data.Rotation = tc.Rotation;
            data.Scale = tc.Scale;
        }

        // Static Mesh
        if ( entity.HasComponent<ECS::StaticMeshComponent>() )
        {
            const auto& smc = entity.GetComponent<ECS::StaticMeshComponent>();
            Assets::StaticMeshComponentSer meshSer;
            
            if ( smc.MeshHandle )
            {
                auto asset = assetManager.FindByHandle<Assets::MeshAsset>( smc.MeshHandle );
                if ( asset )
                {
                    meshSer.MeshPath = asset->GetMetadata().Filepath.string();
                }
            }

            meshSer.MaterialSlots = smc.MaterialSlots;
            if ( !smc.MaterialSlots.empty() )
            {
                meshSer.MaterialPaths = std::vector<std::string>{};
                for ( auto handle : smc.MaterialSlots )
                {
                    auto matAsset = assetManager.FindByHandle<Assets::MaterialAsset>( handle );
                    meshSer.MaterialPaths->push_back( matAsset ? matAsset->GetMetadata().Filepath.string() : "" );
                }
            }
            meshSer.Primitive = smc.Primitive;

            if ( smc.RuntimeMesh )
            {
                const auto& vertices = smc.RuntimeMesh->GetVertices();
                const auto& indices  = smc.RuntimeMesh->GetIndices();
                
                std::vector<Assets::VertexSer> customVertices;
                for ( const auto& v : vertices )
                {
                    customVertices.push_back( { .Position = v.Position,
                                                .Normal   = v.Normal,
                                                .TexCoord = v.TexCoord } );
                }
                meshSer.CustomVertices = customVertices;

                std::vector<uint32_t> flattenedIndices;
                for ( const auto& i : indices )
                {
                    flattenedIndices.push_back( i.V1 );
                    flattenedIndices.push_back( i.V2 );
                    flattenedIndices.push_back( i.V3 );
                }
                meshSer.CustomIndices = flattenedIndices;
            }

            data.StaticMesh = meshSer;
        }

        // Skinned Mesh
        if ( entity.HasComponent<ECS::SkinnedMeshComponent>() )
        {
            const auto& smc = entity.GetComponent<ECS::SkinnedMeshComponent>();
            Assets::SkinnedMeshComponentSer meshSer;
            
            if ( smc.MeshHandle )
            {
                auto asset = assetManager.FindByHandle<Assets::MeshAsset>( smc.MeshHandle );
                if ( asset )
                {
                    meshSer.MeshPath = asset->GetMetadata().Filepath.string();
                }
            }

            meshSer.MaterialSlots = smc.MaterialSlots;
            if ( !smc.MaterialSlots.empty() )
            {
                meshSer.MaterialPaths = std::vector<std::string>{};
                for ( auto handle : smc.MaterialSlots )
                {
                    auto matAsset = assetManager.FindByHandle<Assets::MaterialAsset>( handle );
                    meshSer.MaterialPaths->push_back( matAsset ? matAsset->GetMetadata().Filepath.string() : "" );
                }
            }
            data.SkinnedMesh = meshSer;
        }

        // Camera
        if ( entity.HasComponent<ECS::CameraComponent>() )
        {
            data.Camera = Assets::CameraComponentSer{ .IsMainCamera = entity.GetComponent<ECS::CameraComponent>().IsMainCamera };
        }

        // Lights
        if ( entity.HasComponent<ECS::DirectionLightComponent>() )
        {
            const auto& light = entity.GetComponent<ECS::DirectionLightComponent>();
            data.DirectionLight =
                 Assets::DirectionLightComponentSer{ .Color = light.Color, .Intensity = light.Intensity };
        }

        if ( entity.HasComponent<ECS::PointLightComponent>() )
        {
            const auto& light = entity.GetComponent<ECS::PointLightComponent>();
            data.PointLight = Assets::PointLightComponentSer{ .Color = light.Color, .Intensity = light.Intensity, .Radius = light.Radius };
        }

        // Skybox
        if ( entity.HasComponent<ECS::SkyboxComponent>() )
        {
            const auto& skybox = entity.GetComponent<ECS::SkyboxComponent>();
            Assets::SkyboxComponentSer skyboxSer;
            skyboxSer.Intensity = skybox.Intensity;

            if ( skybox.SkyboxHandle != 0 )
            {
                auto asset = assetManager.FindByHandle<Assets::SkyboxAsset>( skybox.SkyboxHandle );
                if ( asset )
                    skyboxSer.SkyboxPath = asset->GetMetadata().Filepath.string();
            }
            data.Skybox = skyboxSer;
        }

        return data;
    }

    void EntitySerializer::DeserializeEntity( const Assets::EntityData& data, ECS::Entity entity, const Assets::AssetManager& assetManager )
    {
        // Tag — entity is always created with TagComponent; avoid double AddComponent
        if ( data.Tag )
        {
            if ( entity.HasComponent<ECS::TagComponent>() )
                entity.GetComponent<ECS::TagComponent>().Tag = *data.Tag;
            else
                entity.AddComponent<ECS::TagComponent>().Tag = *data.Tag;
        }

        // Transform
        if ( data.Translation || data.Rotation || data.Scale )
        {
            auto& tc = entity.GetComponent<ECS::TransformComponent>();
            if ( data.Translation ) tc.Translation = *data.Translation;
            if ( data.Rotation )    tc.Rotation = *data.Rotation;
            if ( data.Scale )       tc.Scale = *data.Scale;
        }

        // Static Mesh
        if ( data.StaticMesh )
        {
            auto& smc = entity.AddComponent<ECS::StaticMeshComponent>();
            
            if ( data.StaticMesh->MeshPath )
            {
                auto asset = assetManager.FindByPath<Assets::MeshAsset>( *data.StaticMesh->MeshPath );
                if ( !asset )
                {
                    auto newAsset = const_cast<Assets::AssetManager&>(assetManager).CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, *data.StaticMesh->MeshPath );
                    if ( newAsset )
                    {
                        Runtime::ResourceRegistry::GetMeshService()->Register( newAsset );
                        newAsset->Load();
                        asset = newAsset;
                    }
                }

                if ( asset )
                {
                    smc.MeshHandle = asset->GetMetadata().Handle;
                }
            }

            if ( data.StaticMesh->MaterialPaths.has_value() && !data.StaticMesh->MaterialPaths->empty() )
            {
                smc.MaterialSlots.clear();
                for ( const auto& path : *data.StaticMesh->MaterialPaths )
                {
                    if ( path.empty() )
                    {
                        smc.MaterialSlots.push_back( {} );
                        continue;
                    }
                    auto matAsset = assetManager.FindByPath<Assets::MaterialAsset>( path );
                    smc.MaterialSlots.push_back( matAsset ? matAsset->GetMetadata().Handle : Assets::AssetHandle{} );
                }
            }
            else
            {
                smc.MaterialSlots = data.StaticMesh->MaterialSlots;
            }
            smc.Primitive = data.StaticMesh->Primitive;

            if ( data.StaticMesh->CustomVertices && data.StaticMesh->CustomIndices )
            {
                std::vector<Vertex> vertices;
                for ( const auto& vs : *data.StaticMesh->CustomVertices )
                {
                    Vertex v;
                    v.Position = vs.Position;
                    v.Normal   = vs.Normal;
                    v.TexCoord = vs.TexCoord;
                    vertices.push_back( v );
                }

                std::vector<Index> indices;
                const auto& rawIndices = *data.StaticMesh->CustomIndices;
                for ( size_t i = 0; i + 2 < rawIndices.size(); i += 3 )
                {
                    indices.push_back( { rawIndices[i], rawIndices[i+1], rawIndices[i+2] } );
                }

                smc.RuntimeMesh = std::make_shared<DynamicMesh>( vertices, indices, std::vector<Submesh>{} );
                smc.RuntimeMesh->Invalidate();
            }
        }

        // Skinned Mesh
        if ( data.SkinnedMesh )
        {
            auto& smc = entity.AddComponent<ECS::SkinnedMeshComponent>();
            
            if ( data.SkinnedMesh->MeshPath )
            {
                auto asset = assetManager.FindByPath<Assets::MeshAsset>( *data.SkinnedMesh->MeshPath );
                if ( !asset )
                {
                    auto newAsset = const_cast<Assets::AssetManager&>(assetManager).CreateAsset<Assets::SkinnedMeshAsset>( Assets::AssetPriority::High, *data.SkinnedMesh->MeshPath );
                    if ( newAsset )
                    {
                        Runtime::ResourceRegistry::GetMeshService()->Register( newAsset );
                        newAsset->Load();
                        asset = newAsset;
                    }
                }

                if ( asset )
                {
                    smc.MeshHandle = asset->GetMetadata().Handle;
                }
            }

            if ( data.SkinnedMesh->MaterialPaths.has_value() && !data.SkinnedMesh->MaterialPaths->empty() )
            {
                smc.MaterialSlots.clear();
                for ( const auto& path : *data.SkinnedMesh->MaterialPaths )
                {
                    if ( path.empty() )
                    {
                        smc.MaterialSlots.push_back( {} );
                        continue;
                    }
                    auto matAsset = assetManager.FindByPath<Assets::MaterialAsset>( path );
                    smc.MaterialSlots.push_back( matAsset ? matAsset->GetMetadata().Handle : Assets::AssetHandle{} );
                }
            }
            else
            {
                smc.MaterialSlots = data.SkinnedMesh->MaterialSlots;
            }
        }

        // Camera
        if ( data.Camera )
        {
            auto& cam = entity.AddComponent<ECS::CameraComponent>();
            cam.IsMainCamera = data.Camera->IsMainCamera;
        }

        // Lights
        if ( data.DirectionLight )
        {
            auto& light = entity.AddComponent<ECS::DirectionLightComponent>();
            light.Color     = data.DirectionLight->Color.value_or( glm::vec3( 1.0f ) );
            light.Intensity = data.DirectionLight->Intensity;
        }

        if ( data.PointLight )
        {
            auto& light = entity.AddComponent<ECS::PointLightComponent>();
            light.Color = data.PointLight->Color;
            light.Intensity = data.PointLight->Intensity;
            light.Radius = data.PointLight->Radius;
        }

        // Skybox
        if ( data.Skybox )
        {
            auto& skybox = entity.AddComponent<ECS::SkyboxComponent>();
            skybox.Intensity = data.Skybox->Intensity;
            
            if ( data.Skybox->SkyboxPath )
            {
                auto skyboxAsset = assetManager.FindByPath<Assets::SkyboxAsset>( *data.Skybox->SkyboxPath );
                if ( !skyboxAsset )
                {
                    skyboxAsset = const_cast<Assets::AssetManager&>( assetManager ).CreateAsset<Assets::SkyboxAsset>(
                        Assets::AssetPriority::Medium, *data.Skybox->SkyboxPath );
                }
                if ( skyboxAsset )
                {
                    if ( !skyboxAsset->IsReadyForUse() )
                        skyboxAsset->Load();
                    if ( !Runtime::ResourceRegistry::GetSkyboxService()->Get( skyboxAsset->GetMetadata().Handle ) )
                        Runtime::ResourceRegistry::GetSkyboxService()->Register( skyboxAsset );
                    skybox.SkyboxHandle = skyboxAsset->GetMetadata().Handle;
                }
            }
        }

        if ( data.PrefabPath )
        {
            auto asset = assetManager.FindByPath<Assets::PrefabAsset>( *data.PrefabPath );
            if ( asset )
            {
                entity.AddComponent<ECS::PrefabComponent>().Prefab = asset->GetMetadata().Handle;
            }
        }
    }
} // namespace Desert::Core::Serialize