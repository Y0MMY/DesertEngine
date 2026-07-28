#include "ComponentRegistry.hpp"
#include <Common/Utilities/FileSystem.hpp>

#include <cstring>

#include <Engine/ECS/Components.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/TextureAsset.hpp>
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

        // ScriptComponent has no reflected data block (reflection can't do std::string/variant lists), so it
        // gets a manual serializer via a reflect-cpp mirror: the .lua path + the exposed-property values.
        struct ScriptPropSer
        {
            std::string Name;
            int         Type = 0;
            double      Number = 0.0;
            bool        Bool = false;
            std::string Str;
        };
        struct ScriptSlotSer
        {
            std::string                Path;
            std::vector<ScriptPropSer> Props;
        };
        // One entity runs a LIST of scripts (single-script legacy format removed).
        struct ScriptCompSer
        {
            std::optional<std::vector<ScriptSlotSer>> Scripts;
        };

        // Rebuild a ScriptSlot's properties from its serialized form.
        auto loadProps = []( const std::vector<ScriptPropSer>& src )
        {
            std::vector<Scripting::ScriptProperty> out;
            for ( const auto& p : src )
            {
                Scripting::ScriptProperty prop;
                prop.Name   = p.Name;
                prop.Type   = static_cast<Scripting::PropertyType>( p.Type );
                prop.Number = p.Number;
                prop.Bool   = p.Bool;
                prop.Str    = p.Str;
                out.push_back( prop );
            }
            return out;
        };

        ComponentSerializer MakeScript()
        {
            ComponentSerializer s;
            s.Key = "Script";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::ScriptComponent>(); };

            s.Serialize = []( ECS::Entity e, const Assets::AssetManager& ) -> rfl::Generic
            {
                const auto&   sc = e.GetComponent<ECS::ScriptComponent>();
                ScriptCompSer ser;
                std::vector<ScriptSlotSer> slots;
                for ( const auto& slot : sc.Scripts )
                {
                    ScriptSlotSer ss;
                    ss.Path = slot.ScriptPath;
                    for ( const auto& p : slot.Properties )
                        ss.Props.push_back( { p.Name, static_cast<int>( p.Type ), p.Number, p.Bool, p.Str } );
                    slots.push_back( std::move( ss ) );
                }
                ser.Scripts = std::move( slots );
                return ToGeneric( ser );
            };

            s.Deserialize = []( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& )
            {
                auto ser = FromGeneric<ScriptCompSer>( g );
                if ( !ser )
                    return;
                auto& sc = e.HasComponent<ECS::ScriptComponent>() ? e.GetComponent<ECS::ScriptComponent>()
                                                                  : e.AddComponent<ECS::ScriptComponent>();
                sc.Scripts.clear();
                if ( ser->Scripts )
                {
                    for ( const auto& ss : *ser->Scripts )
                    {
                        ECS::ScriptSlot slot;
                        slot.ScriptPath = ss.Path;
                        slot.Started    = false;
                        slot.Properties = loadProps( ss.Props );
                        sc.Scripts.push_back( std::move( slot ) );
                    }
                }
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
                if ( type == "TextureAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::TextureAsset>( Common::UUID( handle ) );
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
                        auto created = m.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, path );
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
                if ( type == "TextureAsset" )
                {
                    // Textures are registered from cooked paths by the preloader; just look up by path.
                    auto a = mgr.FindByPath<Assets::TextureAsset>( path );
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

            // Asset-database path: a stable GUID stored in the scene resolves directly through the
            // AssetManager (assets adopt their persisted/path-derived handles on load), surviving
            // file renames that break path references. Returns 0 for unknown GUIDs so the caller
            // falls back to FromPath.
            r.FromGuid = [&mgr]( uint64_t guid, const std::string& type ) -> uint64_t
            {
                if ( guid == 0 )
                    return 0;
                const Common::UUID handle( guid );

                if ( type == "MaterialAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::MaterialAsset>( handle );
                    if ( !a )
                        return 0;
                    if ( auto* svc = Runtime::ResourceRegistry::GetMaterialService(); svc && !svc->Get( handle ) )
                        svc->Register( a );
                    return guid;
                }
                if ( type == "TextureAsset" )
                {
                    return mgr.FindByHandle<Assets::TextureAsset>( handle ) ? guid : 0;
                }
                if ( type == "StaticMeshAsset" || type == "SkinnedMeshAsset" || type == "MeshAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::MeshAsset>( handle );
                    if ( !a )
                        return 0;
                    if ( auto* svc = Runtime::ResourceRegistry::GetMeshService();
                         svc && !svc->GetAsset( handle ) )
                    {
                        svc->Register( a );
                        a->Load();
                    }
                    return guid;
                }
                if ( type == "SkyboxAsset" )
                {
                    return mgr.FindByHandle<Assets::SkyboxAsset>( handle ) ? guid : 0;
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
                {
                    if ( auto p = resolver.ToPath( static_cast<uint64_t>( smc.MeshHandle ), "StaticMeshAsset" );
                         !p.empty() )
                        meshSer.MeshPath = p;
                    // GUID = the stable handle itself (asset-database identity); rename-safe.
                    meshSer.MeshGuid = static_cast<uint64_t>( smc.MeshHandle );
                }

                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    meshSer.MaterialGuids = std::vector<uint64_t>{};
                    for ( auto handle : smc.MaterialSlots )
                    {
                        meshSer.MaterialPaths->push_back(
                             resolver.ToPath( static_cast<uint64_t>( handle ), "MaterialAsset" ) );
                        meshSer.MaterialGuids->push_back( static_cast<uint64_t>( handle ) );
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

                auto& smc      = entity.AddComponent<ECS::StaticMeshComponent>();
                auto  resolver = MakeAssetResolver( assetManager );

                // GUID first (rename-safe asset-database reference), path as fallback/back-compat.
                uint64_t meshHandle = 0;
                if ( meshData.MeshGuid && resolver.FromGuid )
                    meshHandle = resolver.FromGuid( *meshData.MeshGuid, "StaticMeshAsset" );
                if ( meshHandle == 0 && meshData.MeshPath )
                    meshHandle = resolver.FromPath( *meshData.MeshPath, "StaticMeshAsset" );
                if ( meshHandle != 0 )
                    smc.MeshHandle = Common::UUID( meshHandle );

                const size_t slotCount = meshData.MaterialGuids
                                              ? meshData.MaterialGuids->size()
                                              : ( meshData.MaterialPaths ? meshData.MaterialPaths->size() : 0 );
                if ( slotCount > 0 )
                {
                    smc.MaterialSlots.clear();
                    for ( size_t i = 0; i < slotCount; ++i )
                    {
                        uint64_t h = 0;
                        if ( meshData.MaterialGuids && i < meshData.MaterialGuids->size() &&
                             resolver.FromGuid )
                            h = resolver.FromGuid( ( *meshData.MaterialGuids )[i], "MaterialAsset" );
                        if ( h == 0 && meshData.MaterialPaths && i < meshData.MaterialPaths->size() )
                            h = resolver.FromPath( ( *meshData.MaterialPaths )[i], "MaterialAsset" );
                        smc.MaterialSlots.push_back( Common::UUID( h ) );
                    }
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

        // ---- Instanced Static Mesh (UE-style ISM: asset/primitive mesh + N world matrices) ----
        {
            ComponentSerializer s;
            s.Key = "InstancedStaticMesh";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::InstancedStaticMeshComponent>(); };

            s.Serialize = []( ECS::Entity entity, const Assets::AssetManager& assetManager ) -> rfl::Generic
            {
                const auto&                             ism = entity.GetComponent<ECS::InstancedStaticMeshComponent>();
                Assets::InstancedStaticMeshComponentSer ser;

                auto resolver = MakeAssetResolver( assetManager );
                if ( ism.MeshHandle )
                    if ( auto p = resolver.ToPath( static_cast<uint64_t>( ism.MeshHandle ), "StaticMeshAsset" );
                         !p.empty() )
                        ser.MeshPath = p;

                if ( !ism.MaterialSlots.empty() )
                {
                    ser.MaterialPaths = std::vector<std::string>{};
                    for ( auto handle : ism.MaterialSlots )
                        ser.MaterialPaths->push_back(
                             resolver.ToPath( static_cast<uint64_t>( handle ), "MaterialAsset" ) );
                }
                ser.Primitive = ism.Primitive;
                if ( !ism.InstanceTransforms.empty() )
                {
                    std::vector<std::array<float, 16>> flat;
                    flat.reserve( ism.InstanceTransforms.size() );
                    for ( const auto& m : ism.InstanceTransforms )
                    {
                        std::array<float, 16> a{};
                        std::memcpy( a.data(), &m[0][0], sizeof( float ) * 16 );
                        flat.push_back( a );
                    }
                    ser.InstanceTransforms = std::move( flat );
                }

                return ToGeneric( ser );
            };

            s.Deserialize =
                 []( ECS::Entity entity, const rfl::Generic& g, const Assets::AssetManager& assetManager )
            {
                auto parsed = FromGeneric<Assets::InstancedStaticMeshComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& data = parsed.value();

                auto& ism      = entity.AddComponent<ECS::InstancedStaticMeshComponent>();
                auto  resolver = MakeAssetResolver( assetManager );

                if ( data.MeshPath )
                    ism.MeshHandle = Common::UUID( resolver.FromPath( *data.MeshPath, "StaticMeshAsset" ) );
                if ( data.MaterialPaths.has_value() )
                {
                    ism.MaterialSlots.clear();
                    for ( const auto& path : *data.MaterialPaths )
                        ism.MaterialSlots.push_back( Common::UUID( resolver.FromPath( path, "MaterialAsset" ) ) );
                }
                ism.Primitive = data.Primitive;
                if ( data.InstanceTransforms.has_value() )
                {
                    ism.InstanceTransforms.clear();
                    ism.InstanceTransforms.reserve( data.InstanceTransforms->size() );
                    for ( const auto& a : *data.InstanceTransforms )
                    {
                        glm::mat4 m( 1.0f );
                        std::memcpy( &m[0][0], a.data(), sizeof( float ) * 16 );
                        ism.InstanceTransforms.push_back( m );
                    }
                }
            };

            Register( std::move( s ) );
        }

        // ---- Material (generic data-driven: shader name + param overrides + texture refs as paths) ----
        {
            ComponentSerializer s;
            s.Key = "Material";
            s.Has = []( ECS::Entity e ) { return e.HasComponent<ECS::MaterialComponent>(); };

            s.Serialize = []( ECS::Entity entity, const Assets::AssetManager& assetManager ) -> rfl::Generic
            {
                const auto&                  mc = entity.GetComponent<ECS::MaterialComponent>();
                Assets::MaterialComponentSer ser;
                ser.ShaderName = mc.ShaderName;

                if ( !mc.Params.empty() )
                {
                    std::vector<Assets::MaterialParamSer> ps;
                    for ( const auto& p : mc.Params )
                        ps.push_back( { p.Name, p.Value } );
                    ser.Params = std::move( ps );
                }

                if ( !mc.Textures.empty() )
                {
                    auto                                    resolver = MakeAssetResolver( assetManager );
                    std::vector<Assets::MaterialTextureSer> ts;
                    for ( const auto& t : mc.Textures )
                        ts.push_back( { t.Name, resolver.ToPath( t.TextureHandle, "TextureAsset" ),
                                        t.TextureHandle } );
                    ser.Textures = std::move( ts );
                }

                return ToGeneric( ser );
            };

            s.Deserialize =
                 []( ECS::Entity entity, const rfl::Generic& g, const Assets::AssetManager& assetManager )
            {
                auto parsed = FromGeneric<Assets::MaterialComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& data = parsed.value();

                auto& mc      = entity.AddComponent<ECS::MaterialComponent>();
                mc.ShaderName = data.ShaderName;

                if ( data.Params.has_value() )
                    for ( const auto& p : *data.Params )
                        mc.Params.push_back( { p.Name, p.Value } );

                if ( data.Textures.has_value() )
                {
                    auto resolver = MakeAssetResolver( assetManager );
                    for ( const auto& t : *data.Textures )
                    {
                        // GUID first (rename-safe), then the legacy path.
                        uint64_t h = ( t.Guid && resolver.FromGuid )
                                          ? resolver.FromGuid( *t.Guid, "TextureAsset" )
                                          : 0;
                        if ( h == 0 )
                            h = resolver.FromPath( t.Path, "TextureAsset" );
                        mc.Textures.push_back( { t.Name, h } );
                    }
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
                {
                    if ( auto p = resolver.ToPath( static_cast<uint64_t>( smc.MeshHandle ), "SkinnedMeshAsset" );
                         !p.empty() )
                        meshSer.MeshPath = p;
                    meshSer.MeshGuid = static_cast<uint64_t>( smc.MeshHandle );
                }

                if ( !smc.MaterialSlots.empty() )
                {
                    meshSer.MaterialPaths = std::vector<std::string>{};
                    meshSer.MaterialGuids = std::vector<uint64_t>{};
                    for ( auto handle : smc.MaterialSlots )
                    {
                        meshSer.MaterialPaths->push_back(
                             resolver.ToPath( static_cast<uint64_t>( handle ), "MaterialAsset" ) );
                        meshSer.MaterialGuids->push_back( static_cast<uint64_t>( handle ) );
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

                auto& smc      = entity.AddComponent<ECS::SkinnedMeshComponent>();
                auto  resolver = MakeAssetResolver( assetManager );

                uint64_t meshHandle = 0;
                if ( meshData.MeshGuid && resolver.FromGuid )
                    meshHandle = resolver.FromGuid( *meshData.MeshGuid, "SkinnedMeshAsset" );
                if ( meshHandle == 0 && meshData.MeshPath )
                    meshHandle = resolver.FromPath( *meshData.MeshPath, "SkinnedMeshAsset" );
                if ( meshHandle != 0 )
                    smc.MeshHandle = Common::UUID( meshHandle );

                const size_t slotCount = meshData.MaterialGuids
                                              ? meshData.MaterialGuids->size()
                                              : ( meshData.MaterialPaths ? meshData.MaterialPaths->size() : 0 );
                if ( slotCount > 0 )
                {
                    smc.MaterialSlots.clear();
                    for ( size_t i = 0; i < slotCount; ++i )
                    {
                        uint64_t h = 0;
                        if ( meshData.MaterialGuids && i < meshData.MaterialGuids->size() &&
                             resolver.FromGuid )
                            h = resolver.FromGuid( ( *meshData.MaterialGuids )[i], "MaterialAsset" );
                        if ( h == 0 && meshData.MaterialPaths && i < meshData.MaterialPaths->size() )
                            h = resolver.FromPath( ( *meshData.MaterialPaths )[i], "MaterialAsset" );
                        smc.MaterialSlots.push_back( Common::UUID( h ) );
                    }
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
        Register( MakeReflected<ECS::TerrainComponent, ECS::TerrainData>( "Terrain", "TerrainData",
                                                                          &ECS::TerrainComponent::Data ) );
        Register( MakeReflected<ECS::ColliderComponent, ECS::ColliderData>( "Collider", "ColliderData",
                                                                            &ECS::ColliderComponent::Data ) );
        Register( MakeReflected<ECS::RigidBodyComponent, ECS::RigidBodyData>( "RigidBody", "RigidBodyData",
                                                                              &ECS::RigidBodyComponent::Data ) );
        Register( MakeReflected<ECS::CharacterControllerComponent, ECS::CharacterControllerData>(
             "CharacterController", "CharacterControllerData", &ECS::CharacterControllerComponent::Data ) );
        Register( MakeReflected<ECS::AudioSourceComponent, ECS::AudioSourceData>(
             "AudioSource", "AudioSourceData", &ECS::AudioSourceComponent::Data ) );

        // ---- Skybox (now FULLY REFLECTED via RA3) ----
        // No more hand-written SkyboxComponentSer / field mapping: the whole component reflects, and its
        // SkyboxHandle round-trips as a path through the AssetResolver. (RequestBake has no PROPERTY → it's
        // excluded automatically.) Compat note: old scenes stored the HDR under key "SkyboxPath"; the
        // reflected field is "SkyboxHandle", so an old HDR selection needs re-pick — procedural sky +
        // clouds carry over (those field names are unchanged).
        Register( MakeReflectedSelf<ECS::SkyboxComponent>( "Skybox", "SkyboxComponent" ) );

        // ---- Script (manual: .lua path + exposed-property values) ----
        Register( MakeScript() );
    }

    std::string SaveMaterialComponentToJson( const ECS::MaterialComponent& mc, const Assets::AssetManager& mgr )
    {
        Assets::MaterialComponentSer ser;
        ser.ShaderName = mc.ShaderName;

        if ( !mc.Params.empty() )
        {
            std::vector<Assets::MaterialParamSer> ps;
            for ( const auto& p : mc.Params )
                ps.push_back( { p.Name, p.Value } );
            ser.Params = std::move( ps );
        }
        if ( !mc.Textures.empty() )
        {
            auto                                    resolver = MakeAssetResolver( mgr );
            std::vector<Assets::MaterialTextureSer> ts;
            for ( const auto& t : mc.Textures )
                ts.push_back(
                     { t.Name, resolver.ToPath( t.TextureHandle, "TextureAsset" ), t.TextureHandle } );
            ser.Textures = std::move( ts );
        }
        return rfl::json::write( ser );
    }

    bool LoadMaterialComponentFromJson( const std::string& json, ECS::MaterialComponent& mc,
                                        const Assets::AssetManager& mgr )
    {
        auto parsed = rfl::json::read<Assets::MaterialComponentSer>( json );
        if ( !parsed )
            return false;
        const auto& data = parsed.value();

        mc.ShaderName = data.ShaderName;
        mc.Params.clear();
        mc.Textures.clear();

        if ( data.Params.has_value() )
            for ( const auto& p : *data.Params )
                mc.Params.push_back( { p.Name, p.Value } );

        if ( data.Textures.has_value() )
        {
            auto resolver = MakeAssetResolver( mgr );
            for ( const auto& t : *data.Textures )
            {
                uint64_t h =
                     ( t.Guid && resolver.FromGuid ) ? resolver.FromGuid( *t.Guid, "TextureAsset" ) : 0;
                if ( h == 0 )
                    h = resolver.FromPath( t.Path, "TextureAsset" );
                mc.Textures.push_back( { t.Name, h } );
            }
        }
        return true;
    }
} // namespace Desert::Core::Serialize
