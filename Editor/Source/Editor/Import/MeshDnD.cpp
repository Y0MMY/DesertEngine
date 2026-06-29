#include "MeshDnD.hpp"
#include "ImportManager.hpp"

#include <Engine/Assets/Mesh/MeshAsset.hpp>
#include <Engine/Assets/Mesh/StaticMeshAsset.hpp>
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

        // Source (Resources/Mesh/foo.obj) -> deterministic cooked path (Cooked/Meshes/foo.stmesh). Mirrors
        // ImportManager::BuildCookedPath / the File Explorer thumbnail mapping.
        std::filesystem::path CookedStaticMeshPath( const std::string& sourcePath )
        {
            std::error_code       ec;
            std::filesystem::path cooked =
                 Common::Constants::Path::MESH_PATH_COOKED /
                 std::filesystem::relative( sourcePath, Common::Constants::Path::MESH_PATH, ec );
            cooked.replace_extension( ".stmesh" );
            return cooked;
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
        return created->GetMetadata().Handle;
    }

} // namespace Desert::Editor::MeshDnD
