#include "MeshDnD.hpp"
#include "ImportManager.hpp"
#include "CookPaths.hpp"

#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SkinnedMeshAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Common/Core/Constants.hpp>

#include <filesystem>

namespace Desert::Editor::MeshDnD
{
    namespace
    {
        // One shared importer for drag-drop cooks (the cooked-file freshness check dedups re-cooks).
        ImportManager& Importer()
        {
            static ImportManager s_Importer;
            return s_Importer;
        }

        // Source (Resources/Assets/Meshes/foo.obj) -> deterministic cooked path (Cooked/Meshes/foo.stmesh).
        std::filesystem::path CookedStaticMeshPath( const std::string& sourcePath )
        {
            return CookPaths::CookedMesh( sourcePath, ".stmesh" );
        }

        // Same, for a rigged source that cooks to a skinned mesh (Cooked/Meshes/foo.skmesh).
        std::filesystem::path CookedSkinnedMeshPath( const std::string& sourcePath )
        {
            return CookPaths::CookedMesh( sourcePath, ".skmesh" );
        }

        // Register every cooked texture (Cooked/Textures/*.tex) into the AssetManager + TextureService so a
        // just-imported material's texture handles RESOLVE this session. MaterialFactory binds textures
        // EAGERLY at material-register time (GetTextureService()->Get(handle)), so textures MUST be registered
        // BEFORE the materials — otherwise the bind silently no-ops and the slot shows "missing" until the
        // next launch (when AssetPreloader scans them). Idempotent (skips already-registered). Mirrors the
        // preloader's texture loop. The mesh's cook already produced these .tex via ExtractMaterials.
        void RegisterCookedTextures( Assets::AssetManager& mgr )
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path  root = Common::Constants::Path::TEXTURE_PATH_COOKED;
            if ( !fs::exists( root, ec ) )
                return;

            for ( const auto& f : fs::recursive_directory_iterator( root, ec ) )
            {
                if ( !f.is_regular_file( ec ) || f.path().extension() != ".tex" )
                    continue;
                const std::string p = f.path().generic_string();
                auto asset = mgr.FindByPath<Assets::TextureAsset>( p );
                if ( !asset )
                    asset = mgr.CreateAsset<Assets::TextureAsset>( Assets::AssetPriority::Low, p );
                if ( !asset )
                    continue;
                if ( !asset->IsReadyForUse() )
                    asset->Load(); // syncs metadata handle to the .tex handle (material refs key by it)
                if ( !Runtime::ResourceRegistry::GetTextureService()->Get( asset->GetMetadata().Handle ) )
                    Runtime::ResourceRegistry::GetTextureService()->Register( asset );
            }
        }

        // The cook's own suffix for a rig, spelled here because Common::Constants::Extensions has no entry
        // for it and AssetPreloader's scan list carries the same literal.
        constexpr const char* kSkeletonExtension = ".skeleton";

        // Register every cooked skeleton (Cooked/Meshes/*.skeleton) into the AssetManager so a
        // just-imported RIGGED mesh finds its rig THIS session.
        //
        // WHY THIS HAS TO EXIST AT ALL: AssetPreloader.cpp was, until now, the only place in the engine that
        // ever constructed a SkeletonAsset. A drop that cooks a new .skmesh also cooks its .skeleton, and
        // nothing put that file in the manager — so the mesh's signature matched nothing, MeshFactory
        // refused to build it, and the character only appeared after a relaunch. Same shape as the textures
        // and materials above: the cook wrote a file the session cannot see.
        void RegisterCookedSkeletons( Assets::AssetManager& mgr )
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path  root = Common::Constants::Path::MESH_PATH_COOKED;
            if ( !fs::exists( root, ec ) )
                return;

            for ( const auto& f : fs::recursive_directory_iterator( root, ec ) )
            {
                if ( !f.is_regular_file( ec ) || f.path().extension() != kSkeletonExtension )
                    continue;

                const std::string p = f.path().generic_string();
                if ( mgr.FindByPath<Assets::SkeletonAsset>( p ) )
                    continue; // idempotent: a drop re-scans the whole directory every time

                // Eager, like the preloader's scan: a skeleton is small, and its signature is what every
                // skinned mesh in the project is matched against — an unloaded one reports 0 and matches
                // nothing.
                if ( !mgr.CreateAsset<Assets::SkeletonAsset>( Assets::AssetPriority::Low, p ) )
                    LOG_ERROR( "Cooked skeleton '{}' could not be registered.", p );
            }
        }

        // Create + register a freshly-imported mesh's materials so their stable external id
        // (PBRSurfaceParams::MaterialId, baked into each submesh) resolves in MaterialService THIS session.
        // Without this the materials would only register on the NEXT launch (AssetPreloader scan) and a
        // just-imported mesh shows "Unassigned material slot". Import writes them as editable content at
        // CookPaths::MaterialFolder(source) (see ImportManager::SerializeMaterialAsset).
        // Idempotent (skips already-registered).
        //
        // Takes the SOURCE path and asks CookPaths, rather than rebuilding the folder from the cooked
        // path's stem. The old spelling here, `MATERIAL_PATH / cookedMeshPath.stem()`, was a second copy
        // of the writer's formula that happened to agree with it only because both discarded the
        // directory — so making the writer directory-aware without this would have left the reader looking
        // in a folder nothing is written to, and every freshly imported mesh unassigned until relaunch.
        void RegisterCookedMaterials( Assets::AssetManager& mgr, const std::filesystem::path& sourcePath )
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path  dir = CookPaths::MaterialFolder( sourcePath );
            if ( !fs::exists( dir, ec ) )
                return;

            for ( const auto& f : fs::directory_iterator( dir, ec ) )
            {
                if ( f.path().extension() != Common::Constants::Extensions::MATERIAL_EXTENSION )
                    continue;

                const std::string matPath = f.path().generic_string();
                auto asset = mgr.FindByPath<Assets::SurfaceMaterialAsset>( matPath );
                if ( !asset )
                    asset = mgr.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, matPath );
                if ( !asset )
                    continue;
                if ( !asset->IsReadyForUse() )
                    asset->Load(); // Load reads MaterialId -> external handle, so Register maps it correctly
                if ( !Runtime::ResourceRegistry::GetMaterialService()->Get( asset->GetMetadata().Handle ) )
                    Runtime::ResourceRegistry::GetMaterialService()->Register( asset );
            }
        }
    } // namespace

    Assets::AssetHandle ResolveOrImport( Assets::AssetManager& mgr, const std::string& sourcePath )
    {
        const std::string cookedStr = CookedStaticMeshPath( sourcePath ).generic_string();

        // Already cooked + registered? Reuse it.
        if ( auto existing = mgr.FindByPath<Assets::MeshAsset>( cookedStr ) )
            return existing->GetMetadata().Handle;

        // Not cooked yet -> cook the source (Assimp parse -> Cooked/Meshes/*.stmesh).
        if ( !std::filesystem::exists( cookedStr ) )
            Importer().Import( sourcePath );

        if ( !std::filesystem::exists( cookedStr ) )
            return Common::UUID::Null(); // cook failed / produced a skinned mesh (.skmesh) instead

        // Create + register + load the cooked static mesh, return its handle.
        auto created = mgr.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, cookedStr );
        if ( !created )
            return Common::UUID::Null();

        Runtime::ResourceRegistry::GetMeshService()->Register( created );
        created->Load();

        // UE-style: a just-imported mesh's materials + textures are immediately available (no restart/Save).
        // Order matters: textures FIRST (materials bind them eagerly at register time), then materials.
        RegisterCookedTextures( mgr );
        RegisterCookedMaterials( mgr, sourcePath );
        return created->GetMetadata().Handle;
    }

    namespace
    {
        // Finalize a SKINNED mesh asset so its GPU build works.
        //
        // The rig goes in FIRST. A shell's skeleton is matched by a signature that only exists once the
        // .skmesh is parsed, so the load below is the moment the lookup becomes possible — and the lookup
        // can only succeed against skeletons the manager already holds. On a fresh import that file was
        // written seconds ago by the cook and is in nobody's registry.
        //
        // This used to spell the load as `Load()` followed by `ResolveDependencies()`. Those two statements
        // are now one call, because the second is the one that gets forgotten (AssetBase::EnsureLoaded), and
        // MeshService's own lazy path forgot it for every mesh that was not dropped by hand.
        Assets::AssetHandle FinalizeSkinned( Assets::AssetManager&                     mgr,
                                             const std::shared_ptr<Assets::MeshAsset>& asset,
                                             const std::string&                        sourcePath )
        {
            RegisterCookedSkeletons( mgr );

            if ( const auto loaded = asset->EnsureLoaded( mgr ); !loaded )
            {
                LOG_ERROR( "Skinned mesh '{}' could not be finalized: {}", asset->GetMetadata().Filepath.string(),
                           loaded.GetError() );
                return Common::UUID::Null();
            }

            Runtime::ResourceRegistry::GetMeshService()->Register( asset ); // rebuild with the resolved skeleton
            RegisterCookedTextures( mgr );
            RegisterCookedMaterials( mgr, sourcePath );
            return asset->GetMetadata().Handle;
        }
    } // namespace

    ResolvedMesh ResolveOrImportMesh( Assets::AssetManager& mgr, const std::string& sourcePath )
    {
        const std::string staticStr  = CookedStaticMeshPath( sourcePath ).generic_string();
        const std::string skinnedStr = CookedSkinnedMeshPath( sourcePath ).generic_string();

        // Already cooked + registered? Reuse it — but a skinned shell from the preloader is lazy + has an
        // UNRESOLVED skeleton, so finalize it (load + resolve + rebuild) instead of returning it raw.
        if ( auto existing = mgr.FindByPath<Assets::MeshAsset>( skinnedStr ) )
            return { FinalizeSkinned( mgr, existing, sourcePath ), true };
        if ( auto existing = mgr.FindByPath<Assets::MeshAsset>( staticStr ) )
            return { existing->GetMetadata().Handle, false };

        // Cook on demand if neither cooked form exists yet (the cook decides static vs skinned by the rig).
        if ( !std::filesystem::exists( staticStr ) && !std::filesystem::exists( skinnedStr ) )
            Importer().Import( sourcePath );

        const bool isSkinned = std::filesystem::exists( skinnedStr );
        const bool isStatic  = std::filesystem::exists( staticStr );
        if ( !isSkinned && !isStatic )
            return { Common::UUID::Null(), false }; // cook failed

        if ( isSkinned )
        {
            // Created as an UNPARSED shell on purpose: loading here would resolve the skeleton against a
            // manager that does not hold the rig yet — the cook wrote the .skeleton one line ago — and the
            // resolve would then never be repeated. FinalizeSkinned registers the rig and loads, in that
            // order, and is the only place either happens.
            auto created = mgr.CreateAsset<Assets::SkinnedMeshAsset>( Assets::AssetPriority::High, skinnedStr,
                                                                      /*loadAfterCreate=*/false );
            if ( !created )
                return { Common::UUID::Null(), false };
            return { FinalizeSkinned( mgr, created, sourcePath ), true };
        }

        auto created = mgr.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, staticStr );
        if ( !created )
            return { Common::UUID::Null(), false };

        Runtime::ResourceRegistry::GetMeshService()->Register( created );
        RegisterCookedTextures( mgr );
        RegisterCookedMaterials( mgr, sourcePath );
        return { created->GetMetadata().Handle, false };
    }

} // namespace Desert::Editor::MeshDnD
