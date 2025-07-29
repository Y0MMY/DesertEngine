#include "AssetPreloader.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 1> SUPPORTED_MESH_EXTENSIONS   = { ".fbx" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKYBOX_EXTENSIONS = { ".hdr" };

    AssetPreloader::AssetPreloader( const std::shared_ptr<AssetManager>&              assetManager,
                                    const std::shared_ptr<Runtime::ResourceRegistry>& resourceRegistry )
         : m_AssetManager( assetManager ), m_ResourceRegistry( resourceRegistry )
    {
    }

    void AssetPreloader::PreloadAllAssets()
    {
        PreloadMeshes();
        PreloadSkyboxes();
    }

    namespace
    {
        template <typename AssetType, typename... Args>
        void ProcessAssetFiles( const std::filesystem::path& rootPath, bool useRootpath,
                                const std::array<std::string_view, 1>& supportedExtensions,
                                const std::weak_ptr<AssetManager>& assetManager, AssetPriority priority,
                                Args&&... args )
        {
            namespace fs = std::filesystem;

            for ( const auto& entry : fs::recursive_directory_iterator( rootPath ) )
            {
                if ( !entry.is_regular_file() )
                    continue;

                std::string ext = entry.path().extension().string();
                std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );

                if ( std::find( supportedExtensions.begin(), supportedExtensions.end(), ext ) ==
                     supportedExtensions.end() )
                    continue;

                if ( auto manager = assetManager.lock() )
                {
                    Common::Filepath path;
                    if ( useRootpath )
                    {
                        path = rootPath / entry.path();
                    }
                    else
                    {
                        path = entry.path();
                    }
                    auto asset = manager->CreateAsset<AssetType>( priority, path, std::forward<Args>( args )... );

                    if ( !asset->GetMetadata().IsValid() )
                    {
                        LOG_ERROR( "Asset metadata is invalid for: {}", path.string() );
                    }
                }
            }
        }
    } // namespace

    void AssetPreloader::PreloadMeshes()
    {
        ProcessAssetFiles<MeshAsset>( Common::Constants::Path::MESH_PATH, false, SUPPORTED_MESH_EXTENSIONS,
                                      m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<MaterialAsset>( Common::Constants::Path::MESH_PATH, false, SUPPORTED_MESH_EXTENSIONS,
                                          m_AssetManager, AssetPriority::Low );

        if ( auto registry = m_ResourceRegistry.lock() )
        {
            if ( auto manager = m_AssetManager.lock() )
            {
                for ( const auto& [handle, meshAsset] : manager->FindAllByType<Assets::MeshAsset>() )
                {
                    registry->RegisterMesh( meshAsset );
                }
            }
        }
    }

    void AssetPreloader::PreloadSkyboxes()
    {
        /*  ProcessAssetFiles<SkyboxAsset>( Common::Constants::Path::SKYBOX_PATH, false,
           SUPPORTED_SKYBOX_EXTENSIONS, m_AssetManager, AssetPriority::Medium );*/
    }
} // namespace Desert::Assets