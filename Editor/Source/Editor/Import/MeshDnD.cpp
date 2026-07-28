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

        // Create + register a freshly-imported mesh's materials so their stable external id
        // (PBRSurfaceParams::MaterialId, baked into each submesh) resolves in MaterialService THIS session.
        // Without this the materials would only register on the NEXT launch (AssetPreloader scan) and a
        // just-imported mesh shows "Unassigned material slot". Import writes them as editable content at
        // Resources/Assets/Materials/<meshStem>/*.demat (see ImportManager::SerializeMaterialAsset).
        // Idempotent (skips already-registered).
        void RegisterCookedMaterials( Assets::AssetManager& mgr, const std::filesystem::path& cookedMeshPath )
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path  dir = Common::Constants::Path::MATERIAL_PATH / cookedMeshPath.stem();
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
        RegisterCookedMaterials( mgr, cookedStr );
        return created->GetMetadata().Handle;
    }

    namespace
    {
        // Finalize a SKINNED mesh asset so its GPU build works. A preloader shell is lazy (loadAfterCreate=false)
        // → its skeleton signature was 0 when ResolveDependencies first ran, so the skeleton is UNRESOLVED.
        // Load it (fills the signature), re-resolve the skeleton, then (re)Register so MeshFactory::CreateSkinned
        // builds with the skeleton in place — otherwise Get() returns null and the character never renders.
        Assets::AssetHandle FinalizeSkinned( Assets::AssetManager&                      mgr,
                                             const std::shared_ptr<Assets::MeshAsset>&  asset,
                                             const std::string&                         skinnedStr )
        {
            if ( !asset->IsReadyForUse() )
                asset->Load();
            asset->ResolveDependencies( mgr );
            Runtime::ResourceRegistry::GetMeshService()->Register( asset ); // rebuild with the resolved skeleton
            RegisterCookedTextures( mgr );
            RegisterCookedMaterials( mgr, skinnedStr );
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
            return { FinalizeSkinned( mgr, existing, skinnedStr ), true };
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
            auto created = mgr.CreateAsset<Assets::SkinnedMeshAsset>( Assets::AssetPriority::High, skinnedStr );
            if ( !created )
                return { Common::UUID::Null(), false };
            return { FinalizeSkinned( mgr, created, skinnedStr ), true };
        }

        auto created = mgr.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, staticStr );
        if ( !created )
            return { Common::UUID::Null(), false };

        Runtime::ResourceRegistry::GetMeshService()->Register( created );
        RegisterCookedTextures( mgr );
        RegisterCookedMaterials( mgr, staticStr );
        return { created->GetMetadata().Handle, false };
    }

} // namespace Desert::Editor::MeshDnD
