#include "ComponentRegistry.hpp"

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Mesh/PBRMaterialAsset.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Assets/Prefab/PrefabData.hpp>
#include <Engine/Geometry/DynamicMesh.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>

#include <rflcpp/rfl/json.hpp>

#include <limits>

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

        // Resolves reflected AssetHandle fields to/from on-disk PATHS (backward-compatible with the old
        // per-component serializers). Dispatches by the field's PROPERTY(Asset<...>) type name. Only a few
        // asset types exist, so this is a small hand-written table (not codegen). Captures `mgr` by ref —
        // only used within the (de)serialize call that builds it.
        Reflection::AssetResolver MakeAssetResolver( const Assets::AssetManager& mgr )
        {
            Reflection::AssetResolver r;

            r.ToPath = [&mgr]( uint64_t handle, const std::string& type ) -> std::string
            {
                if ( handle == 0 )
                    return "";
                if ( type == "SkyboxAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::SkyboxAsset>( Common::UUID( handle ) );
                    return a ? a->GetMetadata().Filepath.string() : "";
                }
                if ( type == "MaterialAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::MaterialAsset>( Common::UUID( handle ) );
                    return a ? a->GetMetadata().Filepath.string() : "";
                }
                // Meshes (static/skinned both resolve handle->path via the MeshAsset base).
                auto a = mgr.FindByHandle<Assets::MeshAsset>( Common::UUID( handle ) );
                return a ? a->GetMetadata().Filepath.string() : "";
            };

            r.FromPath = [&mgr]( const std::string& path, const std::string& type ) -> uint64_t
            {
                if ( path.empty() )
                    return 0;
                auto& m = const_cast<Assets::AssetManager&>( mgr );

                if ( type == "SkyboxAsset" )
                {
                    auto a = mgr.FindByPath<Assets::SkyboxAsset>( path );
                    if ( !a )
                        a = m.CreateAsset<Assets::SkyboxAsset>( Assets::AssetPriority::Medium, path );
                    if ( a )
                    {
                        if ( !a->IsReadyForUse() )
                            a->Load();
                        if ( !Runtime::ResourceRegistry::GetSkyboxService()->Get( a->GetMetadata().Handle ) )
                            Runtime::ResourceRegistry::GetSkyboxService()->Register( a );
                        return static_cast<uint64_t>( a->GetMetadata().Handle );
                    }
                    return 0;
                }
                if ( type == "MaterialAsset" )
                {
                    auto a = mgr.FindByPath<Assets::MaterialAsset>( path );
                    if ( !a )
                    {
                        // Not preloaded (e.g. an editor .demat the preloader's .mat scan missed). Create +
                        // load + register from the path so materials survive a cold restart, not just an
                        // in-session reload.
                        auto created = m.CreateAsset<Assets::PBRMaterialAsset>( Assets::AssetPriority::High, path );
                        if ( created )
                        {
                            if ( !created->IsReadyForUse() )
                                created->Load();
                            if ( !Runtime::ResourceRegistry::GetMaterialService()->Get(
                                      created->GetMetadata().Handle ) )
                                Runtime::ResourceRegistry::GetMaterialService()->Register( created );
                            a = created;
                        }
                    }
                    return a ? static_cast<uint64_t>( a->GetMetadata().Handle ) : 0;
                }
                // Meshes: find, else cook-create as the concrete type + register + load.
                if ( type == "StaticMeshAsset" || type == "SkinnedMeshAsset" || type == "MeshAsset" )
                {
                    auto a = mgr.FindByPath<Assets::MeshAsset>( path );
                    if ( !a )
                    {
                        Assets::Asset<Assets::MeshAsset> created;
                        if ( type == "SkinnedMeshAsset" )
                            created = m.CreateAsset<Assets::SkinnedMeshAsset>( Assets::AssetPriority::High, path );
                        else
                            created = m.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, path );
                        if ( created )
                        {
                            Runtime::ResourceRegistry::GetMeshService()->Register( created );
                            created->Load();
                            a = created;
                        }
                    }
                    return a ? static_cast<uint64_t>( a->GetMetadata().Handle ) : 0;
                }
                return 0;
            };

            return r;
        }

        // Like MakeReflected, but reflects the WHOLE component (no Data sub-member) and threads an
        // AssetResolver so AssetHandle fields round-trip as paths. Used for asset-bearing components that
        // are now fully reflected (Skybox) instead of hand-mapped.
        template <class TComponent>
        ComponentSerializer MakeReflectedSelf( std::string key, std::string typeName )
        {
            ComponentSerializer s;
            s.Key = std::move( key );
            s.Has = []( ECS::Entity e ) { return e.HasComponent<TComponent>(); };

            s.Serialize = [typeName]( ECS::Entity e, const Assets::AssetManager& mgr ) -> rfl::Generic
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return rfl::Generic( rfl::Generic::Object{} );
                auto resolver = MakeAssetResolver( mgr );
                return Reflection::SerializeReflected( *type, &e.GetComponent<TComponent>(), &resolver );
            };

            s.Deserialize = [typeName]( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& mgr )
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return;
                auto obj = g.to_object();
                if ( !obj.has_value() )
                    return;
                auto  resolver = MakeAssetResolver( mgr );
                auto& comp =
                     e.HasComponent<TComponent>() ? e.GetComponent<TComponent>() : e.AddComponent<TComponent>();
                Reflection::DeserializeReflected( *type, &comp, obj.value(), &resolver );
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

                // Asset refs go through the single shared resolver (same code path as the reflected
                // components), instead of duplicating FindByHandle/FindByPath here.
                auto resolver = MakeAssetResolver( assetManager );
                if ( smc.MeshHandle )
                    if ( auto p = resolver.ToPath( static_cast<uint64_t>( smc.MeshHandle ), "StaticMeshAsset" );
                         !p.empty() )
                        meshSer.MeshPath = p;

                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    for ( auto handle : smc.MaterialSlots )
                        meshSer.MaterialPaths->push_back(
                             resolver.ToPath( static_cast<uint64_t>( handle ), "MaterialAsset" ) );
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

                auto& smc      = entity.AddComponent<ECS::StaticMeshComponent>();
                auto  resolver = MakeAssetResolver( assetManager );

                if ( meshData.MeshPath )
                    smc.MeshHandle = Common::UUID( resolver.FromPath( *meshData.MeshPath, "StaticMeshAsset" ) );

                if ( meshData.MaterialPaths.has_value() )
                {
                    smc.MaterialSlots.clear();
                    for ( const auto& path : *meshData.MaterialPaths )
                        smc.MaterialSlots.push_back( Common::UUID( resolver.FromPath( path, "MaterialAsset" ) ) );
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

                    // One full-range submesh — without it the renderer (which draws per-submesh) draws
                    // nothing. Compute the AABB from the verts. (The old code passed an empty submesh
                    // list, so reconstructed/edited meshes were invisible on load.)
                    Common::Math::AABB aabb;
                    aabb.Min = glm::vec3( std::numeric_limits<float>::max() );
                    aabb.Max = glm::vec3( std::numeric_limits<float>::lowest() );
                    for ( const auto& v : vertices )
                    {
                        aabb.Min = glm::min( aabb.Min, v.Position );
                        aabb.Max = glm::max( aabb.Max, v.Position );
                    }
                    if ( vertices.empty() )
                    {
                        aabb.Min = glm::vec3( 0.0f );
                        aabb.Max = glm::vec3( 0.0f );
                    }

                    std::vector<Submesh> submeshes = {
                        { "Mesh", 0, static_cast<uint32_t>( vertices.size() ), 0,
                          static_cast<uint32_t>( indices.size() ) * 3, glm::mat4( 1.0f ), aabb } };

                    smc.RuntimeMesh = std::make_shared<DynamicMesh>( vertices, indices, submeshes );
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

                auto resolver = MakeAssetResolver( assetManager );
                if ( smc.MeshHandle )
                    if ( auto p = resolver.ToPath( static_cast<uint64_t>( smc.MeshHandle ), "SkinnedMeshAsset" );
                         !p.empty() )
                        meshSer.MeshPath = p;

                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    for ( auto handle : smc.MaterialSlots )
                        meshSer.MaterialPaths->push_back(
                             resolver.ToPath( static_cast<uint64_t>( handle ), "MaterialAsset" ) );
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

                auto& smc      = entity.AddComponent<ECS::SkinnedMeshComponent>();
                auto  resolver = MakeAssetResolver( assetManager );

                if ( meshData.MeshPath )
                    smc.MeshHandle = Common::UUID( resolver.FromPath( *meshData.MeshPath, "SkinnedMeshAsset" ) );

                if ( meshData.MaterialPaths.has_value() )
                {
                    smc.MaterialSlots.clear();
                    for ( const auto& path : *meshData.MaterialPaths )
                        smc.MaterialSlots.push_back( Common::UUID( resolver.FromPath( path, "MaterialAsset" ) ) );
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

        // ---- Skybox (now FULLY REFLECTED via RA3) ----
        // No more hand-written SkyboxComponentSer / field mapping: the whole component reflects, and its
        // SkyboxHandle round-trips as a path through the AssetResolver. (RequestBake has no PROPERTY → it's
        // excluded automatically.) Compat note: old scenes stored the HDR under key "SkyboxPath"; the
        // reflected field is "SkyboxHandle", so an old HDR selection needs re-pick — procedural sky +
        // clouds carry over (those field names are unchanged).
        Register( MakeReflectedSelf<ECS::SkyboxComponent>( "Skybox", "SkyboxComponent" ) );
    }
} // namespace Desert::Core::Serialize
