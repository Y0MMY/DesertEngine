#include "ComponentRegistry.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Assets/Prefab/PrefabData.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>

#include <rflcpp/rfl/json.hpp>

namespace Desert::Core::Serialize
{
    namespace
    {
        // Bridges a typed serialization struct to/from the generic JSON tree, reusing reflect-cpp's own
        // (de)serialization for the verbose asset-bearing payloads (mesh vertices, material path lists).
        template <class T>
        rfl::Generic ToGeneric( const T& value )
        {
            auto g = rfl::json::read<rfl::Generic>( rfl::json::write( value ) );
            return g.has_value() ? g.value() : rfl::Generic( rfl::Generic::Object{} );
        }

        template <class T>
        std::optional<T> FromGeneric( const rfl::Generic& g )
        {
            auto r = rfl::json::read<T>( rfl::json::write( g ) );
            if ( r.has_value() )
                return r.value();
            return std::nullopt;
        }

        // Builds a handler for a component whose serializable payload is a reflected data block. Adding a
        // PROPERTY field to that block automatically extends serialization — no code change here.
        template <class TComponent, class TData>
        ComponentSerializer MakeReflected( std::string key, std::string typeName, TData TComponent::*member )
        {
            ComponentSerializer s;
            s.Key = std::move( key );
            s.Has = []( ECS::Entity e ) { return e.HasComponent<TComponent>(); };

            s.Serialize = [member, typeName]( ECS::Entity e, const Assets::AssetManager& ) -> rfl::Generic
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return rfl::Generic( rfl::Generic::Object{} );
                const auto& comp = e.GetComponent<TComponent>();
                return Reflection::SerializeReflected( *type, &( comp.*member ) );
            };

            s.Deserialize = [member, typeName]( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& )
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return;
                auto obj = g.to_object();
                if ( !obj.has_value() )
                    return;
                auto& comp =
                     e.HasComponent<TComponent>() ? e.GetComponent<TComponent>() : e.AddComponent<TComponent>();
                Reflection::DeserializeReflected( *type, &( comp.*member ), obj.value() );
            };

            return s;
        }
    } // namespace

    const ComponentRegistry& ComponentRegistry::Get()
    {
        static ComponentRegistry instance;
        return instance;
    }

    ComponentRegistry::ComponentRegistry()
    {
        RegisterBuiltins();
    }

    void ComponentRegistry::Register( ComponentSerializer serializer )
    {
        m_Serializers.push_back( std::move( serializer ) );
    }

    void ComponentRegistry::RegisterBuiltins()
    {
        // ---- Static Mesh (asset-bearing: handle <-> path) ----
        {
            ComponentSerializer s;
            s.Key = "StaticMesh";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::StaticMeshComponent>(); };

            s.Serialize = []( ECS::Entity entity, const Assets::AssetManager& assetManager ) -> rfl::Generic
            {
                const auto&                    smc = entity.GetComponent<ECS::StaticMeshComponent>();
                Assets::StaticMeshComponentSer meshSer;

                if ( smc.MeshHandle )
                {
                    auto asset = assetManager.FindByHandle<Assets::MeshAsset>( smc.MeshHandle );
                    if ( asset )
                        meshSer.MeshPath = asset->GetMetadata().Filepath.string();
                }

                meshSer.MaterialSlots = smc.MaterialSlots;
                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    for ( auto handle : smc.MaterialSlots )
                    {
                        auto matAsset = assetManager.FindByHandle<Assets::MaterialAsset>( handle );
                        meshSer.MaterialPaths->push_back( matAsset ? matAsset->GetMetadata().Filepath.string()
                                                                   : "" );
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
                        customVertices.push_back(
                             { .Position = v.Position, .Normal = v.Normal, .TexCoord = v.TexCoord } );
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

                return ToGeneric( meshSer );
            };

            s.Deserialize =
                 []( ECS::Entity entity, const rfl::Generic& g, const Assets::AssetManager& assetManager )
            {
                auto parsed = FromGeneric<Assets::StaticMeshComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& meshData = parsed.value();

                auto& smc = entity.AddComponent<ECS::StaticMeshComponent>();

                if ( meshData.MeshPath )
                {
                    auto asset = assetManager.FindByPath<Assets::MeshAsset>( *meshData.MeshPath );
                    if ( !asset )
                    {
                        auto newAsset = const_cast<Assets::AssetManager&>( assetManager )
                                             .CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High,
                                                                                   *meshData.MeshPath );
                        if ( newAsset )
                        {
                            Runtime::ResourceRegistry::GetMeshService()->Register( newAsset );
                            newAsset->Load();
                            asset = newAsset;
                        }
                    }

                    if ( asset )
                        smc.MeshHandle = asset->GetMetadata().Handle;
                }

                if ( meshData.MaterialPaths.has_value() && !meshData.MaterialPaths->empty() )
                {
                    smc.MaterialSlots.clear();
                    for ( const auto& path : *meshData.MaterialPaths )
                    {
                        if ( path.empty() )
                        {
                            smc.MaterialSlots.push_back( {} );
                            continue;
                        }
                        auto matAsset = assetManager.FindByPath<Assets::MaterialAsset>( path );
                        smc.MaterialSlots.push_back( matAsset ? matAsset->GetMetadata().Handle
                                                              : Assets::AssetHandle{} );
                    }
                }
                else
                {
                    smc.MaterialSlots = meshData.MaterialSlots;
                }
                smc.Primitive = meshData.Primitive;

                if ( meshData.CustomVertices && meshData.CustomIndices )
                {
                    std::vector<Vertex> vertices;
                    for ( const auto& vs : *meshData.CustomVertices )
                    {
                        Vertex v;
                        v.Position = vs.Position;
                        v.Normal   = vs.Normal;
                        v.TexCoord = vs.TexCoord;
                        vertices.push_back( v );
                    }

                    std::vector<Index> indices;
                    const auto&        rawIndices = *meshData.CustomIndices;
                    for ( size_t i = 0; i + 2 < rawIndices.size(); i += 3 )
                    {
                        indices.push_back( { rawIndices[i], rawIndices[i + 1], rawIndices[i + 2] } );
                    }

                    smc.RuntimeMesh = std::make_shared<DynamicMesh>( vertices, indices, std::vector<Submesh>{} );
                    smc.RuntimeMesh->Invalidate();
                }
            };

            Register( std::move( s ) );
        }

        // ---- Skinned Mesh (asset-bearing) ----
        {
            ComponentSerializer s;
            s.Key = "SkinnedMesh";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::SkinnedMeshComponent>(); };

            s.Serialize = []( ECS::Entity entity, const Assets::AssetManager& assetManager ) -> rfl::Generic
            {
                const auto&                     smc = entity.GetComponent<ECS::SkinnedMeshComponent>();
                Assets::SkinnedMeshComponentSer meshSer;

                if ( smc.MeshHandle )
                {
                    auto asset = assetManager.FindByHandle<Assets::MeshAsset>( smc.MeshHandle );
                    if ( asset )
                        meshSer.MeshPath = asset->GetMetadata().Filepath.string();
                }

                meshSer.MaterialSlots = smc.MaterialSlots;
                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    for ( auto handle : smc.MaterialSlots )
                    {
                        auto matAsset = assetManager.FindByHandle<Assets::MaterialAsset>( handle );
                        meshSer.MaterialPaths->push_back( matAsset ? matAsset->GetMetadata().Filepath.string()
                                                                   : "" );
                    }
                }

                return ToGeneric( meshSer );
            };

            s.Deserialize =
                 []( ECS::Entity entity, const rfl::Generic& g, const Assets::AssetManager& assetManager )
            {
                auto parsed = FromGeneric<Assets::SkinnedMeshComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& meshData = parsed.value();

                auto& smc = entity.AddComponent<ECS::SkinnedMeshComponent>();

                if ( meshData.MeshPath )
                {
                    auto asset = assetManager.FindByPath<Assets::MeshAsset>( *meshData.MeshPath );
                    if ( !asset )
                    {
                        auto newAsset = const_cast<Assets::AssetManager&>( assetManager )
                                             .CreateAsset<Assets::SkinnedMeshAsset>( Assets::AssetPriority::High,
                                                                                    *meshData.MeshPath );
                        if ( newAsset )
                        {
                            Runtime::ResourceRegistry::GetMeshService()->Register( newAsset );
                            newAsset->Load();
                            asset = newAsset;
                        }
                    }

                    if ( asset )
                        smc.MeshHandle = asset->GetMetadata().Handle;
                }

                if ( meshData.MaterialPaths.has_value() && !meshData.MaterialPaths->empty() )
                {
                    smc.MaterialSlots.clear();
                    for ( const auto& path : *meshData.MaterialPaths )
                    {
                        if ( path.empty() )
                        {
                            smc.MaterialSlots.push_back( {} );
                            continue;
                        }
                        auto matAsset = assetManager.FindByPath<Assets::MaterialAsset>( path );
                        smc.MaterialSlots.push_back( matAsset ? matAsset->GetMetadata().Handle
                                                              : Assets::AssetHandle{} );
                    }
                }
                else
                {
                    smc.MaterialSlots = meshData.MaterialSlots;
                }
            };

            Register( std::move( s ) );
        }

        // ---- Reflected data blocks (auto-serialized via reflection) ----
        Register( MakeReflected<ECS::CameraComponent, ECS::CameraData>( "Camera", "CameraData",
                                                                        &ECS::CameraComponent::Data ) );
        Register( MakeReflected<ECS::DirectionLightComponent, ECS::DirectionalLightData>(
             "DirectionLight", "DirectionalLightData", &ECS::DirectionLightComponent::Data ) );
        Register( MakeReflected<ECS::PointLightComponent, ECS::PointLightData>(
             "PointLight", "PointLightData", &ECS::PointLightComponent::Data ) );
        Register( MakeReflected<ECS::SpotLightComponent, ECS::SpotLightData>(
             "SpotLight", "SpotLightData", &ECS::SpotLightComponent::Data ) );

        // ---- Skybox (asset-bearing) ----
        {
            ComponentSerializer s;
            s.Key = "Skybox";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::SkyboxComponent>(); };

            s.Serialize = []( ECS::Entity entity, const Assets::AssetManager& assetManager ) -> rfl::Generic
            {
                const auto&                skybox = entity.GetComponent<ECS::SkyboxComponent>();
                Assets::SkyboxComponentSer skyboxSer;
                skyboxSer.Intensity     = skybox.Intensity;
                skyboxSer.Procedural    = skybox.Procedural;
                skyboxSer.SunIntensity  = skybox.SunIntensity;
                skyboxSer.SunDiskRadius = skybox.SunDiskRadius;

                if ( skybox.SkyboxHandle != 0 )
                {
                    auto asset = assetManager.FindByHandle<Assets::SkyboxAsset>( skybox.SkyboxHandle );
                    if ( asset )
                        skyboxSer.SkyboxPath = asset->GetMetadata().Filepath.string();
                }

                return ToGeneric( skyboxSer );
            };

            s.Deserialize =
                 []( ECS::Entity entity, const rfl::Generic& g, const Assets::AssetManager& assetManager )
            {
                auto parsed = FromGeneric<Assets::SkyboxComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& skyboxData = parsed.value();

                auto& skybox         = entity.AddComponent<ECS::SkyboxComponent>();
                skybox.Intensity     = skyboxData.Intensity;
                skybox.Procedural    = skyboxData.Procedural;
                skybox.SunIntensity  = skyboxData.SunIntensity;
                skybox.SunDiskRadius = skyboxData.SunDiskRadius;

                if ( skyboxData.SkyboxPath )
                {
                    auto skyboxAsset = assetManager.FindByPath<Assets::SkyboxAsset>( *skyboxData.SkyboxPath );
                    if ( !skyboxAsset )
                    {
                        skyboxAsset = const_cast<Assets::AssetManager&>( assetManager )
                                           .CreateAsset<Assets::SkyboxAsset>( Assets::AssetPriority::Medium,
                                                                             *skyboxData.SkyboxPath );
                    }
                    if ( skyboxAsset )
                    {
                        if ( !skyboxAsset->IsReadyForUse() )
                            skyboxAsset->Load();
                        if ( !Runtime::ResourceRegistry::GetSkyboxService()->Get(
                                  skyboxAsset->GetMetadata().Handle ) )
                            Runtime::ResourceRegistry::GetSkyboxService()->Register( skyboxAsset );
                        skybox.SkyboxHandle = skyboxAsset->GetMetadata().Handle;
                    }
                }
            };

            Register( std::move( s ) );
        }
    }
} // namespace Desert::Core::Serialize
