#include "ComponentRegistry.hpp"
#include <Common/Utilities/FileSystem.hpp>

#include <cstring>

#include <Engine/ECS/Components.hpp>
#include <Engine/Animation/Graph/AnimGraph.hpp>
#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/MaterialAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Assets/Skybox/SkyboxAsset.hpp>
#include <Engine/Assets/CloudTypeAsset.hpp>
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

        // Defined below, beside the table of asset types it dispatches on. Declared here because the two
        // template helpers above it call it with a non-dependent argument, so the name has to be visible
        // at their point of definition rather than at instantiation.
        Reflection::AssetResolver MakeAssetResolver( const Assets::AssetManager& mgr );

        // Builds a handler for a component whose serializable payload is a reflected data block. Adding a
        // PROPERTY field to that block automatically extends serialization — no code change here.
        template <class TComponent, class TData>
        ComponentSerializer MakeReflected( std::string key, std::string typeName, TData TComponent::*member )
        {
            ComponentSerializer s;
            s.Key = std::move( key );
            s.Has = []( ECS::Entity e ) { return e.HasComponent<TComponent>(); };

            // The AssetResolver is passed on BOTH directions, exactly as MakeReflectedSelf passes it. It
            // is a no-op for a component with no PROPERTY(Asset<...>) field, and load-bearing for the ones
            // that have one (UIImage's Sprite, UIText's Font, UIPanel's Video). Without it an AssetHandle
            // field is written as a raw 64-bit id and read back as one: the id is minted at load time from
            // the file path, so it does not survive a restart, and the component comes back pointing at
            // nothing with no error anywhere.
            s.Serialize = [member, typeName]( ECS::Entity e, const Assets::AssetManager& mgr ) -> rfl::Generic
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return rfl::Generic( rfl::Generic::Object{} );
                const auto& comp     = e.GetComponent<TComponent>();
                auto        resolver = MakeAssetResolver( mgr );
                return Reflection::SerializeReflected( *type, &( comp.*member ), &resolver );
            };

            s.Deserialize =
                 [member, typeName]( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& mgr )
            {
                const auto* type = Reflection::ReflectionRegistry::Get().Find( typeName );
                if ( !type )
                    return;
                auto obj = g.to_object();
                if ( !obj.has_value() )
                    return;
                auto& comp =
                     e.HasComponent<TComponent>() ? e.GetComponent<TComponent>() : e.AddComponent<TComponent>();
                auto resolver = MakeAssetResolver( mgr );
                Reflection::DeserializeReflected( *type, &( comp.*member ), obj.value(), &resolver );
            };

            return s;
        }

        // A marker component (no data, e.g. FolderComponent): presence IS the state. Serializes as an empty
        // object; deserialize just re-adds it.
        template <class TComponent>
        ComponentSerializer MakeMarker( std::string key )
        {
            ComponentSerializer s;
            s.Key       = std::move( key );
            s.Has       = []( ECS::Entity e ) { return e.HasComponent<TComponent>(); };
            s.Serialize = []( ECS::Entity, const Assets::AssetManager& ) -> rfl::Generic
            { return rfl::Generic( rfl::Generic::Object{} ); };
            s.Deserialize = []( ECS::Entity e, const rfl::Generic&, const Assets::AssetManager& )
            {
                if ( !e.HasComponent<TComponent>() )
                    e.AddComponent<TComponent>();
            };
            return s;
        }

        // ScriptComponent has no reflected data block (reflection can't do std::string/variant lists), so it
        // gets a manual serializer via a reflect-cpp mirror: the .lua path + the exposed-property values.
        struct ScriptPropSer
        {
            std::string Name;
            int         Type   = 0;
            double      Number = 0.0;
            bool        Bool   = false;
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
                const auto&                sc = e.GetComponent<ECS::ScriptComponent>();
                ScriptCompSer              ser;
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
                // NO "CloudNoiseVolumeAsset" BRANCH ANY MORE. It served exactly one reflected field — the
                // cloud layer's own noise slot — and that field moved onto the cloud TYPE, which stores its
                // volume as a path of its own rather than through this resolver. A branch keyed on a
                // metadata string no reflected field produces is a path nothing can reach (§4.1).
                if ( type == "CloudTypeAsset" )
                {
                    auto a = mgr.FindByHandle<Assets::CloudTypeAsset>( Common::UUID( handle ) );
                    if ( !a )
                        return "";

                    // RELATIVE to the assets root, unlike every branch around it, and the difference is
                    // deliberate rather than an oversight. Those branches write absolute paths, so every
                    // scene in this repository carries one developer's home directory and a cloud type
                    // shipped with the engine would be unresolvable on any other machine — while the
                    // library it names is content that ships WITH the project. The v4 -> v5 migration has
                    // to produce this string too, and it is a pure function that cannot read a path root,
                    // so relative is also the only form it could write. FromPath below accepts either.
                    std::error_code ec;
                    const auto      rel = std::filesystem::relative( a->GetMetadata().Filepath,
                                                                     Common::Constants::Path::ASSETS_PATH, ec );
                    if ( ec || rel.empty() || rel.native().rfind( "..", 0 ) == 0 )
                        return a->GetMetadata().Filepath.string(); // outside the project — say so plainly
                    return rel.generic_string();
                }
                if ( type == "FontAsset" )
                {
                    // Fonts aren't AssetManager assets — the FontService owns the handle<->path registry.
                    return Runtime::ResourceRegistry::GetFontService()->PathForHandle( handle );
                }
                if ( type == "VideoAsset" )
                {
                    // Videos aren't AssetManager assets — the VideoService owns the handle<->path registry.
                    return Runtime::ResourceRegistry::GetVideoService()->PathForHandle( handle );
                }
                if ( type == "IconAsset" )
                {
                    // Vector icons aren't AssetManager assets — the IconService owns handle<->path.
                    return Runtime::ResourceRegistry::GetIconService()->PathForHandle( handle );
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
                        auto created =
                             m.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, path );
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
                // The read side of the volume branch is gone with the write side above, and for the same
                // reason: no reflected field names that asset type any more. A cloud type's own volume is
                // bound in Assets::CloudTypeAsset::ResolveDependencies, from the path inside the type's
                // file, which is where a reference to a `.dcnv` now lives.
                if ( type == "CloudTypeAsset" )
                {
                    // BOTH FORMS ARE ACCEPTED, and neither is a legacy path. A file written by ToPath above
                    // (or by the v4 -> v5 migration) carries a path relative to the assets root, because
                    // the library ships with the project; a file an artist points at outside the project
                    // carries an absolute one. Joining a relative path to the root is the whole difference,
                    // and doing it here means it happens exactly once.
                    const std::filesystem::path named( path );
                    const std::filesystem::path full =
                         named.is_absolute() ? named
                                             : ( Common::Constants::Path::ASSETS_PATH / named ).lexically_normal();

                    auto a = mgr.FindByPath<Assets::CloudTypeAsset>( full );
                    if ( !a )
                        a = m.CreateAsset<Assets::CloudTypeAsset>( Assets::AssetPriority::Medium, full );
                    if ( !a )
                        return 0;
                    if ( !a->IsReadyForUse() && !a->Load() )
                        return 0;
                    if ( const auto registered = Runtime::ResourceRegistry::GetCloudTypeService()->Register( a );
                         !registered )
                        LOG_ERROR( "[Clouds] Cloud type '{}' named by the scene could not be registered: {}",
                                   full.string(), registered.GetError() );
                    return static_cast<uint64_t>( a->GetMetadata().Handle );
                }
                if ( type == "FontAsset" )
                {
                    // Register the path with the FontService (idempotent) and return its deterministic handle.
                    return Runtime::ResourceRegistry::GetFontService()->RegisterFont( path );
                }
                if ( type == "VideoAsset" )
                {
                    // Register the path with the VideoService (idempotent) and return its deterministic handle.
                    return Runtime::ResourceRegistry::GetVideoService()->RegisterVideo( path );
                }
                if ( type == "IconAsset" )
                {
                    // Register the .svg with the IconService (idempotent); it bakes the SDF on first draw.
                    return Runtime::ResourceRegistry::GetIconService()->RegisterIcon( path );
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
                    if ( auto* svc = Runtime::ResourceRegistry::GetMeshService(); svc && !svc->GetAsset( handle ) )
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

                // Rendering controls: write only non-default values (absent = default on load).
                if ( smc.OutlineDraw )
                    meshSer.OutlineDraw = smc.OutlineDraw;
                if ( smc.ForcedLOD >= 0 )
                    meshSer.ForcedLOD = smc.ForcedLOD;
                if ( smc.LODBias != 0 )
                    meshSer.LODBias = smc.LODBias;
                if ( !smc.CastShadows )
                    meshSer.CastShadows = smc.CastShadows;
                if ( !smc.ReceiveShadows )
                    meshSer.ReceiveShadows = smc.ReceiveShadows;
                if ( smc.HiddenSubmeshes != 0 )
                    meshSer.HiddenSubmeshes = smc.HiddenSubmeshes;

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
                        if ( meshData.MaterialGuids && i < meshData.MaterialGuids->size() && resolver.FromGuid )
                            h = resolver.FromGuid( ( *meshData.MaterialGuids )[i], "MaterialAsset" );
                        if ( h == 0 && meshData.MaterialPaths && i < meshData.MaterialPaths->size() )
                            h = resolver.FromPath( ( *meshData.MaterialPaths )[i], "MaterialAsset" );
                        smc.MaterialSlots.push_back( Common::UUID( h ) );
                    }
                }
                smc.Primitive = meshData.Primitive;

                smc.OutlineDraw     = meshData.OutlineDraw.value_or( smc.OutlineDraw );
                smc.ForcedLOD       = meshData.ForcedLOD.value_or( smc.ForcedLOD );
                smc.LODBias         = meshData.LODBias.value_or( smc.LODBias );
                smc.CastShadows     = meshData.CastShadows.value_or( smc.CastShadows );
                smc.ReceiveShadows  = meshData.ReceiveShadows.value_or( smc.ReceiveShadows );
                smc.HiddenSubmeshes = meshData.HiddenSubmeshes.value_or( smc.HiddenSubmeshes );

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

                    std::vector<Submesh> submeshes = { { "Mesh", 0, static_cast<uint32_t>( vertices.size() ), 0,
                                                         static_cast<uint32_t>( indices.size() ) * 3,
                                                         glm::mat4( 1.0f ), aabb } };

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
                const auto& ism = entity.GetComponent<ECS::InstancedStaticMeshComponent>();
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
                        ts.push_back(
                             { t.Name, resolver.ToPath( t.TextureHandle, "TextureAsset" ), t.TextureHandle } );
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
                        uint64_t h =
                             ( t.Guid && resolver.FromGuid ) ? resolver.FromGuid( *t.Guid, "TextureAsset" ) : 0;
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
                        if ( meshData.MaterialGuids && i < meshData.MaterialGuids->size() && resolver.FromGuid )
                            h = resolver.FromGuid( ( *meshData.MaterialGuids )[i], "MaterialAsset" );
                        if ( h == 0 && meshData.MaterialPaths && i < meshData.MaterialPaths->size() )
                            h = resolver.FromPath( ( *meshData.MaterialPaths )[i], "MaterialAsset" );
                        smc.MaterialSlots.push_back( Common::UUID( h ) );
                    }
                }
            };

            Register( std::move( s ) );
        }

        // ---- UI Anim (custom: the reflected path has no vector-of-struct support; the playhead is
        //      runtime-only and never written) ----
        {
            ComponentSerializer s;
            s.Key       = "UIAnim";
            s.Has       = []( ECS::Entity e ) { return e.HasComponent<ECS::UIAnimComponent>(); };
            s.Serialize = []( ECS::Entity e, const Assets::AssetManager& ) -> rfl::Generic
            {
                const auto&                d = e.GetComponent<ECS::UIAnimComponent>().Data;
                Assets::UIAnimComponentSer ser;
                ser.Duration = d.Duration;
                ser.Loop     = d.Loop;
                ser.Playing  = d.Playing;
                ser.Tracks.reserve( d.Tracks.size() );
                for ( const auto& tr : d.Tracks )
                {
                    Assets::UIAnimTrackSer ts;
                    ts.Property = static_cast<int>( tr.Property );
                    ts.Keys.reserve( tr.Keys.size() );
                    for ( const auto& k : tr.Keys )
                        ts.Keys.push_back( { k.Time, k.Value, static_cast<int>( k.Easing ) } );
                    ser.Tracks.push_back( std::move( ts ) );
                }
                return ToGeneric( ser );
            };
            s.Deserialize = []( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& )
            {
                auto parsed = FromGeneric<Assets::UIAnimComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& d    = parsed.value();
                auto&       ac   = e.HasComponent<ECS::UIAnimComponent>() ? e.GetComponent<ECS::UIAnimComponent>()
                                                                          : e.AddComponent<ECS::UIAnimComponent>();
                ac.Data.Duration = d.Duration;
                ac.Data.Loop     = d.Loop;
                ac.Data.Playing  = d.Playing;
                ac.Data.Time     = 0.0f;
                ac.Data.Tracks.clear();
                ac.Data.Tracks.reserve( d.Tracks.size() );
                for ( const auto& ts : d.Tracks )
                {
                    ECS::UIAnimTrack tr;
                    tr.Property = static_cast<ECS::UITweenProperty>( ts.Property );
                    tr.Keys.reserve( ts.Keys.size() );
                    for ( const auto& k : ts.Keys )
                        tr.Keys.push_back( { k.Time, k.Value, static_cast<ECS::UIEasing>( k.Easing ) } );
                    std::sort( tr.Keys.begin(), tr.Keys.end(),
                               []( const ECS::UIAnimKey& a, const ECS::UIAnimKey& b )
                               { return a.Time < b.Time; } );
                    ac.Data.Tracks.push_back( std::move( tr ) );
                }
            };
            Register( std::move( s ) );
        }

        // ---- Text (custom: only the authored fields; the glyph mesh is transient) ----
        {
            ComponentSerializer s;
            s.Key       = "Text";
            s.Has       = []( ECS::Entity e ) { return e.HasComponent<ECS::TextComponent>(); };
            s.Serialize = []( ECS::Entity e, const Assets::AssetManager& ) -> rfl::Generic
            {
                const auto& tc = e.GetComponent<ECS::TextComponent>();
                // The font is an asset HANDLE in memory but persists as its stable ttf PATH — keeps scenes
                // portable and backward-compatible with pre-handle saves (which stored the path directly).
                const std::string fontPath = Runtime::ResourceRegistry::GetFontService()->PathForHandle(
                     static_cast<uint64_t>( tc.Font ) );
                Assets::TextComponentSer ser{ tc.Text,     fontPath, tc.Color, tc.Size, tc.EmissiveIntensity,
                                              tc.Billboard };
                return ToGeneric( ser );
            };
            s.Deserialize = []( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& )
            {
                auto parsed = FromGeneric<Assets::TextComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& d        = parsed.value();
                auto&       tc       = e.HasComponent<ECS::TextComponent>() ? e.GetComponent<ECS::TextComponent>()
                                                                            : e.AddComponent<ECS::TextComponent>();
                tc.Text              = d.Text;
                // Path -> stable handle (registers it so the handle resolves at render time). Empty stays null,
                // which the render path falls back to the default font for.
                tc.Font = Assets::AssetHandle(
                     Runtime::ResourceRegistry::GetFontService()->RegisterFont( d.FontPath ) );
                tc.Color             = d.Color;
                tc.Size              = d.Size;
                tc.EmissiveIntensity = d.EmissiveIntensity;
                tc.Billboard         = d.Billboard;
            };
            Register( std::move( s ) );
        }

        // ---- Animation (manual: playback settings + the AnimGraph state machine as JSON) ----
        {
            ComponentSerializer s;
            s.Key       = "Animation";
            s.Has       = []( ECS::Entity e ) { return e.HasComponent<ECS::AnimationComponent>(); };
            s.Serialize = []( ECS::Entity e, const Assets::AssetManager& ) -> rfl::Generic
            {
                const auto&                   ac = e.GetComponent<ECS::AnimationComponent>();
                Assets::AnimationComponentSer ser;
                ser.CurrentClip      = ac.CurrentClip;
                ser.Playing          = ac.Playing;
                ser.Loop             = ac.Loop;
                ser.PlaybackSpeed    = ac.PlaybackSpeed;
                ser.EnableRootMotion = ac.EnableRootMotion;
                if ( ac.Graph )
                    ser.GraphJson = Animation::Graph::Serialize( *ac.Graph );
                return ToGeneric( ser );
            };
            s.Deserialize = []( ECS::Entity e, const rfl::Generic& g, const Assets::AssetManager& )
            {
                auto parsed = FromGeneric<Assets::AnimationComponentSer>( g );
                if ( !parsed.has_value() )
                    return;
                const auto& d = parsed.value();
                auto& ac = e.HasComponent<ECS::AnimationComponent>() ? e.GetComponent<ECS::AnimationComponent>()
                                                                     : e.AddComponent<ECS::AnimationComponent>();
                ac.CurrentClip      = d.CurrentClip;
                ac.Playing          = d.Playing;
                ac.Loop             = d.Loop;
                ac.PlaybackSpeed    = d.PlaybackSpeed;
                ac.EnableRootMotion = d.EnableRootMotion;
                if ( !d.GraphJson.empty() )
                {
                    auto graph = Animation::Graph::Deserialize( d.GraphJson );
                    if ( graph.IsSuccess() )
                    {
                        ac.Graph = std::make_shared<Animation::Graph::AnimGraph>( graph.GetValue() );
                        ac.GraphRevision++;
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
        Register( MakeReflected<ECS::SpotLightComponent, ECS::SpotLightData>( "SpotLight", "SpotLightData",
                                                                              &ECS::SpotLightComponent::Data ) );
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
        Register( MakeReflected<ECS::ParticleEmitterComponent, ECS::ParticleEmitterData>(
             "ParticleEmitter", "ParticleEmitterData", &ECS::ParticleEmitterComponent::Data ) );
        Register( MakeReflected<ECS::UICanvasComponent, ECS::UICanvasData>( "UICanvas", "UICanvasData",
                                                                            &ECS::UICanvasComponent::Data ) );
        Register( MakeReflected<ECS::UILayoutComponent, ECS::UILayoutData>( "UILayout", "UILayoutData",
                                                                            &ECS::UILayoutComponent::Data ) );
        Register( MakeReflected<ECS::UIPanelComponent, ECS::UIPanelData>( "UIPanel", "UIPanelData",
                                                                          &ECS::UIPanelComponent::Data ) );
        Register( MakeReflected<ECS::UITextComponent2D, ECS::UITextData>( "UIText", "UITextData",
                                                                          &ECS::UITextComponent2D::Data ) );
        Register( MakeReflected<ECS::UIButtonComponent, ECS::UIButtonData>( "UIButton", "UIButtonData",
                                                                            &ECS::UIButtonComponent::Data ) );
        Register( MakeReflected<ECS::UIIconComponent, ECS::UIIconData>( "UIIcon", "UIIconData",
                                                                        &ECS::UIIconComponent::Data ) );

        Register( MakeReflected<ECS::UIBindingComponent, ECS::UIBindingData>( "UIBinding", "UIBindingData",
                                                                              &ECS::UIBindingComponent::Data ) );
        Register( MakeReflected<ECS::UIScreenComponent, ECS::UIScreenData>( "UIScreen", "UIScreenData",
                                                                            &ECS::UIScreenComponent::Data ) );
        Register( MakeReflected<ECS::UIScreenStackComponent, ECS::UIScreenStackData>(
             "UIScreenStack", "UIScreenStackData", &ECS::UIScreenStackComponent::Data ) );
        Register( MakeReflected<ECS::UITweenComponent, ECS::UITweenData>( "UITween", "UITweenData",
                                                                          &ECS::UITweenComponent::Data ) );
        Register( MakeReflected<ECS::UIPointerEventsComponent, ECS::UIPointerEventsData>(
             "UIPointerEvents", "UIPointerEventsData", &ECS::UIPointerEventsComponent::Data ) );
        Register( MakeReflected<ECS::UIDraggableComponent, ECS::UIDraggableData>(
             "UIDraggable", "UIDraggableData", &ECS::UIDraggableComponent::Data ) );
        Register( MakeReflected<ECS::UIDropTargetComponent, ECS::UIDropTargetData>(
             "UIDropTarget", "UIDropTargetData", &ECS::UIDropTargetComponent::Data ) );
        Register( MakeReflected<ECS::UIImageComponent, ECS::UIImageData>( "UIImage", "UIImageData",
                                                                          &ECS::UIImageComponent::Data ) );
        Register( MakeReflected<ECS::UILayoutGroupComponent, ECS::UILayoutGroupData>(
             "UILayoutGroup", "UILayoutGroupData", &ECS::UILayoutGroupComponent::Data ) );
        Register( MakeReflected<ECS::UIProgressBarComponent, ECS::UIProgressBarData>(
             "UIProgressBar", "UIProgressBarData", &ECS::UIProgressBarComponent::Data ) );
        Register( MakeReflected<ECS::UIToggleComponent, ECS::UIToggleData>( "UIToggle", "UIToggleData",
                                                                            &ECS::UIToggleComponent::Data ) );
        Register( MakeReflected<ECS::UISliderComponent, ECS::UISliderData>( "UISlider", "UISliderData",
                                                                            &ECS::UISliderComponent::Data ) );
        Register( MakeReflected<ECS::UIScrollViewComponent, ECS::UIScrollViewData>(
             "UIScrollView", "UIScrollViewData", &ECS::UIScrollViewComponent::Data ) );
        Register( MakeReflected<ECS::UIInputFieldComponent, ECS::UIInputFieldData>(
             "UIInputField", "UIInputFieldData", &ECS::UIInputFieldComponent::Data ) );
        Register( MakeReflected<ECS::UIDropdownComponent, ECS::UIDropdownData>(
             "UIDropdown", "UIDropdownData", &ECS::UIDropdownComponent::Data ) );

        // ---- Marker components (presence is the state) ----
        Register( MakeMarker<ECS::FolderComponent>( "Folder" ) );

        // ---- Skybox (now FULLY REFLECTED via RA3) ----
        // No more hand-written SkyboxComponentSer / field mapping: the whole component reflects, and its
        // SkyboxHandle round-trips as a path through the AssetResolver. It now carries the HDR path ONLY —
        // the procedural sky lives under "SkyAtmosphere".
        Register( MakeReflectedSelf<ECS::SkyboxComponent>( "Skybox", "SkyboxComponent" ) );

        // ---- Sky / fog ----
        // Data-block components, so one line each is the whole of save/load, duplicate and undo.
        Register( MakeReflected<ECS::SkyAtmosphereComponent, ECS::SkyAtmosphereData>(
             "SkyAtmosphere", "SkyAtmosphereData", &ECS::SkyAtmosphereComponent::Data ) );
        Register( MakeReflected<ECS::ExponentialHeightFogComponent, ECS::ExponentialHeightFogData>(
             "ExponentialHeightFog", "ExponentialHeightFogData", &ECS::ExponentialHeightFogComponent::Data ) );
        Register( MakeReflected<ECS::VolumetricCloudComponent, ECS::VolumetricCloudData>(
             "VolumetricCloud", "VolumetricCloudData", &ECS::VolumetricCloudComponent::Data ) );

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
                ts.push_back( { t.Name, resolver.ToPath( t.TextureHandle, "TextureAsset" ), t.TextureHandle } );
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
                uint64_t h = ( t.Guid && resolver.FromGuid ) ? resolver.FromGuid( *t.Guid, "TextureAsset" ) : 0;
                if ( h == 0 )
                    h = resolver.FromPath( t.Path, "TextureAsset" );
                mc.Textures.push_back( { t.Name, h } );
            }
        }
        return true;
    }
} // namespace Desert::Core::Serialize
