#include "ImportManager.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>

#include "Assimp/AssimpImporter.hpp"

#include <Common/Core/Constants.hpp>

#include <Engine/Assets/TextureAsset.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

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
        namespace fs = std::filesystem;

        fs::path relative = fs::relative( sourcePath, Common::Constants::Path::MESH_PATH );

        fs::path cookedRoot = "Cooked/Meshes";

        fs::path result = cookedRoot / relative;
        result.replace_extension( extension );

        fs::create_directories( result.parent_path() );

        return result;
    }

    ImportManager::ImportManager()
    {
        m_TextureImporter    = std::make_unique<TextureImporter>();
        m_Importers[".fbx"]  = std::make_unique<AssimpImporter>();
        m_Importers[".obj"]  = std::make_unique<AssimpImporter>();
        m_Importers[".gltf"] = std::make_unique<AssimpImporter>();
        m_Importers[".glb"]  = std::make_unique<AssimpImporter>();
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

        for ( const auto& entry : fs::recursive_directory_iterator( root ) )
        {
            if ( !entry.is_regular_file() )
                continue;

            std::string ext = entry.path().extension().string();
            std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

            if ( m_Importers.contains( ext ) )
            {
                Import( entry.path(), force );
            }
        }
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

        for ( auto& anim : result.Material )
        {
            SerializeMaterialAsset( anim, sourcePath );
        }
    }

    void ImportManager::SerializeMeshAsset( const Desert::Assets::Serialization::MeshAssetData& data,
                                            const std::filesystem::path&                        sourcePath )
    {
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

    void ImportManager::SerializeMaterialAsset( const Desert::Assets::Serialization::MaterialAssetData& data,
                                                const std::filesystem::path& sourcePath )
    {
        auto path = BuildCookedPath( sourcePath, "_" + data.Name + ".mat" );
        WriteJsonToFile( data, path );
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