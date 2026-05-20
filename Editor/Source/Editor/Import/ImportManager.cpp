#include "ImportManager.hpp"
#include <Common/Core/Serialization/GlmReflection.hpp>

#include "Assimp/AssimpImporter.hpp"

#include <Common/Core/Constants.hpp>

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
        m_Importers[".gltf"] = std::make_unique<AssimpImporter>();
        m_Importers[".glb"]  = std::make_unique<AssimpImporter>();
    }

    void ImportManager::Import( const std::filesystem::path& path )
    {
        auto ext = path.extension().string();

        if ( m_Importers.contains( ext ) )
        {
            auto result = m_Importers[ext]->Import( path, *this );

            CreateAssetsFromImport( result, path );
        }
    }

    void ImportManager::ImportAllFromDirectory( const std::filesystem::path& root )
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
                Import( entry.path() );
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

} // namespace Desert::Editor