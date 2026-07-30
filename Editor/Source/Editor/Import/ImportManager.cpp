#include "ImportManager.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>

#include "Assimp/AssimpImporter.hpp"
#include "Blend/BlendImporter.hpp"
#include "CookPaths.hpp"
#include "LODFold.hpp"

#include <Common/Core/Constants.hpp>

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Geometry/MeshLOD.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/JobSystem.hpp>

#include <chrono>
#include <regex>

namespace Desert::Editor
{

    template <typename T>
    void WriteJsonToFile( const T& data, const std::filesystem::path& path )
    {
        auto json = rfl::json::write( data );

        static const std::regex illegal( R"([<>:"/\\|?*])" );

        std::filesystem::create_directories( path.parent_path() );

        std::string filename = path.filename().string();
        filename             = std::regex_replace( filename, illegal, "_" );

        std::filesystem::path fixedPath = path.parent_path() / filename;

        std::ofstream out( fixedPath, std::ios::binary );
        if ( !out.is_open() )
        {
            throw std::runtime_error( "Failed to open file: " + fixedPath.string() );
        }

        out << json;
    }

    static std::filesystem::path BuildCookedPath( const std::filesystem::path& sourcePath,
                                                  const std::string&           extension )
    {
        // Path formula is shared (CookPaths::CookedMesh); this wrapper also ensures the dir exists for writing.
        const auto result = Editor::CookPaths::CookedMesh( sourcePath, extension );
        std::filesystem::create_directories( result.parent_path() );
        return result;
    }

    ImportManager::ImportManager()
    {
        m_TextureImporter     = std::make_unique<TextureImporter>();
        m_Importers[".fbx"]   = std::make_unique<AssimpImporter>();
        m_Importers[".obj"]   = std::make_unique<AssimpImporter>();
        m_Importers[".gltf"]  = std::make_unique<AssimpImporter>();
        m_Importers[".glb"]   = std::make_unique<AssimpImporter>();
        // .blend is converted to FBX via Blender headless, then run through the Assimp path (see BlendImporter).
        m_Importers[".blend"] = std::make_unique<BlendImporter>();
    }

    namespace
    {
        // A cooked file counts as up-to-date if it exists and isn't older than its source.
        bool CookedFresh( const std::filesystem::path& source, const std::filesystem::path& cooked )
        {
            std::error_code ec;
            if ( !std::filesystem::exists( cooked, ec ) )
                return false;
            const auto cookedT = std::filesystem::last_write_time( cooked, ec );
            if ( ec )
                return false;
            const auto srcT = std::filesystem::last_write_time( source, ec );
            if ( ec )
                return false;
            return cookedT >= srcT;
        }
    } // namespace

    void ImportManager::Import( const std::filesystem::path& path, bool force )
    {
        auto ext = path.extension().string();
        std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

        if ( !m_Importers.contains( ext ) )
            return;

        // Skip the expensive Assimp re-parse (+ its texture/material re-cook) when a cooked mesh output
        // already exists and is up-to-date. A source produces either a static or a skinned mesh, so accept
        // either. `force` (Rebuild Cooked Assets) bypasses this.
        if ( !force && ( CookedFresh( path, BuildCookedPath( path, ".stmesh" ) ) ||
                         CookedFresh( path, BuildCookedPath( path, ".skmesh" ) ) ) )
            return;

        auto result = m_Importers[ext]->Import( path, *this );
        CreateAssetsFromImport( result, path );
    }

    void ImportManager::ImportAllFromDirectory( const std::filesystem::path& root, bool force )
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if ( !fs::exists( root, ec ) ) // tolerate a missing source dir (e.g. clean/from-scratch project)
            return;

        // Gather first, cook in PARALLEL after: each source file is an independent CPU+disk job (that is
        // exactly what AsyncMeshLoader relies on for single files). Each worker thread cooks on its OWN
        // ImportManager (Assimp importers are not reentrant); shared cooked-texture writes are serialized
        // inside TextureImporter.
        std::vector<fs::path> files;
        for ( const auto& entry : fs::recursive_directory_iterator( root, ec ) )
        {
            if ( !entry.is_regular_file() )
                continue;

            std::string ext = entry.path().extension().string();
            std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

            // .blend is converted via a (potentially multi-minute) headless Blender run — far too heavy to do
            // for every file during a blocking bulk scan at boot. It's imported ON DEMAND instead (drag-drop
            // goes through AsyncMeshLoader on a worker thread), so the editor never freezes on it.
            if ( ext == ".blend" )
                continue;

            if ( m_Importers.contains( ext ) )
                files.push_back( entry.path() );
        }

        if ( files.empty() )
            return;

        if ( files.size() == 1 )
        {
            Import( files.front(), force );
            return;
        }

        const auto started = std::chrono::steady_clock::now();
        Common::JobSystem::Get().ParallelFor( files.size(),
                                              [&]( size_t i )
                                              {
                                                  // One cooker per worker thread, reused across files.
                                                  thread_local ImportManager s_ThreadImporter;
                                                  s_ThreadImporter.Import( files[i], force );
                                              } );
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started )
                             .count();
        LOG_INFO( "[Import] {} source file(s) checked/cooked in {} ms ({} workers)", files.size(), ms,
                  Common::JobSystem::Get().WorkerCount() );
    }

    void ImportManager::CreateAssetsFromImport( const ImportResult&          result,
                                                const std::filesystem::path& sourcePath )
    {
        if ( result.Mesh )
        {
            SerializeMeshAsset( result.Mesh.value(), sourcePath );
        }

        if ( result.Skeleton )
        {
            SerializeSkeletonAsset( result.Skeleton.value(), sourcePath );
        }

        for ( auto& anim : result.Animations )
        {
            SerializeAnimationAsset( anim, sourcePath );
        }

        for ( auto& material : result.Materials )
        {
            SerializeMaterialAsset( material, sourcePath );
        }
    }

    namespace
    {
        // Bakes each static submesh's LOD triangle sets at cook time (meshopt) into SubmeshData.LODs, so the
        // load path can skip the simplification pass. Skinned meshes are left un-LODed (as before).
        void BakeStaticMeshLODs( Desert::Assets::Serialization::MeshAssetData& data )
        {
            if ( data.IsSkinned )
                return;
            for ( auto& sm : data.Submeshes )
            {
                if ( !sm.LODs.empty() )
                    continue; // author LODs already folded in (FoldExternalLODMeshes) -> don't regenerate

                const uint32_t triCount = sm.IndexCount / 3;
                if ( triCount < 8 || sm.VertexCount == 0 ||
                     sm.VertexOffset + sm.VertexCount > data.StaticVertices.size() )
                    continue;

                std::vector<float> pos;
                pos.reserve( sm.VertexCount * 3 );
                for ( uint32_t v = 0; v < sm.VertexCount; ++v )
                {
                    const auto& p = data.StaticVertices[sm.VertexOffset + v].Position;
                    pos.push_back( p.x );
                    pos.push_back( p.y );
                    pos.push_back( p.z );
                }

                std::vector<Desert::Index> localTris;
                localTris.reserve( triCount );
                const uint32_t triStart = sm.IndexOffset / 3;
                if ( triStart + triCount > data.Indices.size() )
                    continue;
                for ( uint32_t t = 0; t < triCount; ++t )
                {
                    const auto& idx = data.Indices[triStart + t];
                    localTris.push_back( { idx.V1, idx.V2, idx.V3 } );
                }

                const auto levels = Desert::Geometry::SimplifyLODLevels( pos.data(), sm.VertexCount, localTris );
                sm.LODs.clear();
                sm.LODs.reserve( levels.size() );
                for ( const auto& lvl : levels )
                {
                    std::vector<Desert::Assets::Serialization::IndexData> tris;
                    tris.reserve( lvl.size() );
                    for ( const auto& tri : lvl )
                        tris.push_back( { tri.V1, tri.V2, tri.V3 } );
                    sm.LODs.push_back( std::move( tris ) );
                }
            }
        }
    } // namespace

    void ImportManager::SerializeMeshAsset( const Desert::Assets::Serialization::MeshAssetData& dataIn,
                                            const std::filesystem::path&                        sourcePath )
    {
        // Mutable copy so we can bake the LOD chain into it before writing.
        Desert::Assets::Serialization::MeshAssetData data = dataIn;
        FoldExternalLODMeshes( data ); // author "<mesh>_LOD<n>" siblings -> SubmeshData.LODs
        BakeStaticMeshLODs( data );    // generate LODs for submeshes that still have none

        std::filesystem::path cookedPath;
        if ( data.IsSkinned )
        {
            cookedPath = BuildCookedPath( sourcePath, ".skmesh" );
        }
        else
        {
            cookedPath = BuildCookedPath( sourcePath, ".stmesh" );
        }
        WriteJsonToFile( data, cookedPath );
    }

    void ImportManager::SerializeSkeletonAsset( const Desert::Assets::Serialization::SkeletonAssetData& data,
                                                const std::filesystem::path& sourcePath )
    {
        auto cookedPath = BuildCookedPath( sourcePath, ".skeleton" );
        WriteJsonToFile( data, cookedPath );
    }

    void ImportManager::SerializeAnimationAsset( const Desert::Assets::Serialization::AnimationAssetData& data,
                                                 const std::filesystem::path& sourcePath )
    {
        auto cookedPath = BuildCookedPath( sourcePath, "_" + data.Name + ".anim" );
        WriteJsonToFile( data, cookedPath );
    }

    void ImportManager::SerializeMaterialAsset( const ImportedMaterial&      material,
                                                const std::filesystem::path& sourcePath )
    {
        // Imported materials are EDITABLE CONTENT, not cooked intermediates -> write them into the content
        // tree at Resources/Assets/Materials/<meshStem>/<materialName>.demat (browsable + editable in the
        // asset browser, reusable), like UE. Per-mesh subfolder avoids name collisions across imports.
        // Meaningful name, NO handle in it (stable identity lives in the file: PBRSurfaceParams::MaterialId).
        // Unified .demat schema (legacy ".mat" cooker output is gone; SurfaceMaterialAsset::Load still READS old).
        static const std::regex illegal( R"([<>:"/\\|?*\s])" );
        const std::string       safeName = std::regex_replace( material.Name, illegal, "_" );
        const std::filesystem::path path =
             Common::Constants::Path::MATERIAL_PATH / sourcePath.stem() /
             ( safeName + Common::Constants::Extensions::MATERIAL_EXTENSION );

        // Only write if MISSING: re-importing a mesh must NOT clobber the user's edits to its material (UE
        // behaviour — re-import updates geometry, keeps the material asset). Delete the .demat to regenerate.
        std::error_code ec;
        if ( std::filesystem::exists( path, ec ) )
            return;
        // Typed extraction -> unified canon (the only on-disk material format).
        WriteJsonToFile( material.Data.ToMaterialData(), path );
    }

    Common::UUID ImportManager::ImportTexture( const std::filesystem::path& path )
    {
        return m_TextureImporter->Import( path );
    }

    Assets::AssetHandle ImportManager::ImportAndRegisterTexture( Assets::AssetManager&         mgr,
                                                                 const std::filesystem::path& source )
    {
        // Cook the source -> Cooked/Textures/<name>.tex (writes metadata referencing the abs source path).
        m_TextureImporter->Import( source );

        const auto cookedMeta = TextureImporter::CookedMetaPath( source );

        // Create + load the TextureAsset from the cooked .tex (Load reads the handle + source path, and
        // syncs the metadata handle), then register it so TextureService can resolve it at draw time.
        auto asset = mgr.CreateAsset<Assets::TextureAsset>( Assets::AssetPriority::Low, cookedMeta.string() );
        if ( !asset )
        {
            LOG_ERROR( "ImportAndRegisterTexture: failed to create TextureAsset from {}",
                       cookedMeta.string() );
            return Common::UUID::Null();
        }

        Runtime::ResourceRegistry::GetTextureService()->Register( asset );
        return asset->GetMetadata().Handle;
    }

} // namespace Desert::Editor