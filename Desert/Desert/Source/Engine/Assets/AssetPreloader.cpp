#include "AssetPreloader.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 1> SUPPORTED_MESH_EXTENSIONS = { ".fbx" };

    AssetPreloader::AssetPreloader( const std::shared_ptr<AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    void AssetPreloader::PreloadAllAssets()
    {
        PreloadMeshes();
        // Add other asset types here as needed
    }

    void AssetPreloader::PreloadMeshes()
    {
        namespace fs = std::filesystem;

        // Recursively scan MESH_PATH for supported files
        for ( const auto& entry : fs::recursive_directory_iterator( Common::Constants::Path::MESH_PATH ) )
        {
            if ( entry.is_regular_file() )
            {
                ProcessMeshFile( entry.path() );
            }
        }
    }

    void AssetPreloader::ProcessMeshFile( const std::filesystem::path& filePath )
    {
        namespace fs = std::filesystem;

        std::string ext = filePath.extension().string();
        std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

        if ( std::find( SUPPORTED_MESH_EXTENSIONS.begin(), SUPPORTED_MESH_EXTENSIONS.end(), ext ) !=
             SUPPORTED_MESH_EXTENSIONS.end() )
        {
            // Check for serialized version first
            std::filesystem::path serializedPath = filePath;
            serializedPath.replace_extension( Common::Constants::Extensions::MESH_SERIALIZBLE_EXTENSION );

            if ( fs::exists( serializedPath ) )
            {
                // TODO: Handle serialized mesh loading
                // For now, we'll just load the original file

                return;
            }

            // Load the mesh
            if ( auto assetManager = m_AssetManager.lock() )
            {
                auto meshAsset     = assetManager->CreateAsset<MeshAsset>( AssetPriority::Low, filePath );
                auto materialAsset = assetManager->CreateAsset<MaterialAsset>( AssetPriority::Low, filePath );

                if ( meshAsset->GetMetadata().IsValid() && materialAsset->GetMetadata().IsValid() )
                {
                    auto& bundle            = m_MeshAssetBundle.emplace_back();
                    bundle.MeshMetadata     = meshAsset->GetMetadata();
                    bundle.MaterialMetadata = materialAsset->GetMetadata();
                }
            }
        }
    }
} // namespace Desert::Assets