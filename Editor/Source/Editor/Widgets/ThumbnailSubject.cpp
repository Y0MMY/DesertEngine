#include "ThumbnailSubject.hpp"

#include <Editor/Import/CookPaths.hpp>
#include <Editor/Import/MeshMaterial.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <filesystem>

namespace Desert::Editor::ThumbnailSubject
{
    Common::ResultStr<Material> ResolveMaterial( Assets::AssetManager& manager, const std::string& assetPath )
    {
        // Mirrors the component deserializer's create-if-missing logic, which is what a cold start needs:
        // the preloader registers every `.demat` under MATERIAL_PATH, but a material an artist has just
        // dropped in — or one that lives outside that root — is not in the manager yet.
        auto asset = manager.FindByPath<Assets::SurfaceMaterialAsset>( assetPath );
        if ( !asset )
        {
            asset = manager.CreateAsset<Assets::SurfaceMaterialAsset>( Assets::AssetPriority::High, assetPath );
            if ( asset && !asset->IsReadyForUse() )
                asset->Load();
        }
        if ( !asset )
        {
            return Common::MakeFormattedError<Material>(
                 "'{}' is not a material the asset manager will accept", assetPath );
        }

        if ( !Runtime::ResourceRegistry::GetMaterialService()->Get( asset->GetMetadata().Handle ) )
            Runtime::ResourceRegistry::GetMaterialService()->Register( asset );

        Material out;
        out.Handle = asset->GetMetadata().Handle;
        out.Flat   = asset->Data().GetFloat( "AlphaCutoff" ) > 0.0f;
        return Common::MakeSuccess( out );
    }

    Common::ResultStr<Mesh> ResolveMesh( Assets::AssetManager& manager, const std::string& sourcePath )
    {
        // A PURE PATH COMPUTATION, hoisted above every filesystem question: CookPaths::CookedMesh is
        // fs::relative and a string replace, no stat. The `exists` check below is the filesystem question
        // and it stays where it is.
        const std::string cooked = CookPaths::CookedMesh( sourcePath, ".stmesh" ).generic_string();

        std::error_code ec;
        if ( !std::filesystem::exists( cooked, ec ) )
        {
            return Common::MakeFormattedError<Mesh>(
                 "'{}' has not been cooked, so there is no '{}' to photograph. Meshes load only from the "
                 "cooked form; the browser shows the type icon until the import produces one",
                 sourcePath, cooked );
        }

        auto asset = manager.FindByPath<Assets::MeshAsset>( cooked );
        if ( !asset )
        {
            auto created =
                 manager.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, cooked );
            if ( !created )
                return Common::MakeFormattedError<Mesh>( "'{}' could not be created as a static mesh", cooked );

            // REGISTER PARSES BEFORE IT BUILDS (Г15), so this is also the load — and its answer is READ.
            // A `Register` whose result is dropped is how an empty cooked mesh came to be cached as a
            // built one: the caller believed "registered" meant "usable" and nothing said otherwise.
            if ( const auto registered = Runtime::ResourceRegistry::GetMeshService()->Register( created );
                 !registered )
            {
                return Common::MakeFormattedError<Mesh>( "cooked mesh '{}' could not be built: {}", cooked,
                                                         registered.GetError() );
            }
            asset = created;
        }

        const auto* runtimeMesh =
             Runtime::ResourceRegistry::GetMeshService()->Get( asset->GetMetadata().Handle );
        if ( !runtimeMesh || runtimeMesh->GetSubmeshes().empty() )
        {
            return Common::MakeFormattedError<Mesh>(
                 "'{}' built no drawable geometry (a skinned mesh's static buffer is empty by design), so "
                 "a capture would photograph empty sky and file it as the asset",
                 cooked );
        }

        Mesh out;
        out.Handle     = asset->GetMetadata().Handle;
        out.CookedPath = cooked;
        out.Material   = MeshMaterial::ResolveSidecar( manager, sourcePath );
        return Common::MakeSuccess( out );
    }
} // namespace Desert::Editor::ThumbnailSubject
