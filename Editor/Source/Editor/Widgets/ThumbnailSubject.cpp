#include "ThumbnailSubject.hpp"

#include <Editor/Import/CookPaths.hpp>
#include <Editor/Import/MeshMaterial.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Core/Formats/ShaderProgramMeta.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/AssetServiceRegistration.hpp>

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
            return Common::MakeFormattedError<Material>( "'{}' is not a material the asset manager will accept",
                                                         assetPath );
        }

        // ── CAN THE PREVIEW'S DRAW PATH EXECUTE THIS MATERIAL AT ALL? ─────────────────────────────────
        //
        // FOUND BY THE SWEEP, AND ONLY REACHABLE BECAUSE OF IT. A thumbnail is a MESH draw — the material
        // on a sphere or on a card — so it is the mesh path, and the mesh path draws exactly
        // `Core::Formats::kMeshPathDomain`. It refuses anything else BY NAME, at LOG_ERROR, once per
        // attempt (MeshRenderer::DrawGenericMeshes).
        //
        // While a thumbnail existed only for materials somebody browsed to, that never fired: nobody
        // opens the folder holding `CloudRaymarch.demat`. The background sweep photographs every material
        // in the project, so on its first run against this repository it produced three of those errors —
        // Volume, Skybox and Terrain — and then wrote each refusal's empty frame to disk AS THE PICTURE OF
        // THE MATERIAL. A black square that the freshness rule would call correct for ever.
        //
        // Refused here instead, with the domain named, so the browser falls back to the albedo swatch (a
        // true statement about the material) and the sweep skips it once rather than photographing
        // nothing. The predicate is the draw path's OWN — never `IsUserAssignable()`, which is the union
        // of three paths and is exactly the mistake ShaderProgramMeta.hpp warns about above it.
        if ( auto* shaders = Runtime::ResourceRegistry::GetShaderService() )
        {
            const std::string shaderName = asset->Data().EffectiveShaderName();
            if ( const auto shader = shaders->GetByName( shaderName ) )
            {
                const Core::Formats::ShaderDomain domain = shader->GetProgramMeta().Domain;
                if ( !Core::Formats::DrawnByMeshPath( domain ) )
                {
                    return Common::MakeFormattedError<Material>(
                         "'{}' uses the shader '{}', whose domain is {} — the thumbnail is a MESH draw and "
                         "the mesh path executes only {}. Photographing it would write an empty frame and "
                         "file it as the picture of this material",
                         assetPath, shaderName, Core::Formats::ShaderDomainName( domain ),
                         Core::Formats::ShaderDomainName( Core::Formats::kMeshPathDomain ) );
                }
            }
        }

        // Was `if ( !GetMaterialService()->Get( h ) ) Register( a )`. `Get` BUILDS the runtime material on a
        // miss, so the question and the answer were the same call — and the sweep asks it about every
        // material in the project. The registration is a map write now; the build happens when the capture
        // shades with it, which is one frame later and only for the materials actually photographed.
        Runtime::EnsureMaterialRegistered( asset );

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
            asset = manager.CreateAsset<Assets::StaticMeshAsset>( Assets::AssetPriority::High, cooked );
            if ( !asset )
                return Common::MakeFormattedError<Mesh>( "'{}' could not be created as a static mesh", cooked );
        }

        // REGISTER AND BUILD, ON BOTH ROUTES. The registration used to live inside the `if` above, so a
        // cooked mesh the manager ALREADY held — which is every mesh, once AssetPreloader has run — reached
        // the line below having never been offered to the mesh service at all.
        //
        // AND THE REFUSAL NAMES THE CONDITION THAT HELD. What stood here was one message for three
        // different facts, and the words it chose were the rarest one's: a mesh nothing had registered
        // produced "built no drawable geometry (a skinned mesh's static buffer is empty by design)", which
        // sends the next reader to look at rigs. This header's own doc block already promised three
        // distinct refusals; it is the code that had two.
        const auto readiness = Runtime::EnsureMeshDrawable( asset, manager );
        if ( readiness != Runtime::MeshReadiness::Drawable )
        {
            return Common::MakeFormattedError<Mesh>(
                 "{}, so a capture would photograph empty sky and file it as the asset",
                 Runtime::ExplainMeshReadiness( readiness, cooked ) );
        }

        Mesh out;
        out.Handle     = asset->GetMetadata().Handle;
        out.CookedPath = cooked;
        out.Material   = MeshMaterial::ResolveSidecar( manager, sourcePath );
        return Common::MakeSuccess( out );
    }
} // namespace Desert::Editor::ThumbnailSubject
