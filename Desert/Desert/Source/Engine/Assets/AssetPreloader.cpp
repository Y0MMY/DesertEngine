#include "AssetPreloader.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 2> SUPPORTED_MESH_EXTENSIONS    = { ".fbx", ".blend" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKYBOX_EXTENSIONS  = { ".hdr" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SHADERS_EXTENSIONS = { ".glsl" };

    AssetPreloader::AssetPreloader( const std::shared_ptr<AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    void AssetPreloader::PreloadAllAssets()
    {
        PreloadMeshes();
        PreloadShaders();
        PreloadSkyboxes();
    }

    namespace
    {
        template <typename AssetType, typename Extensions, typename... Args>
        void ProcessAssetFiles( const std::filesystem::path& rootPath, bool useRootpath,
                                const Extensions&                  supportedExtensions,
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
                        std::string       originalPath   = entry.path().string();
                        const std::string prefixToRemove = "Resources/Textures/";
                        size_t            pos            = originalPath.find( prefixToRemove );
                        if ( pos != std::string::npos )
                        {
                            originalPath.erase( pos, prefixToRemove.length() );
                        }
                        path = originalPath;
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

        if ( auto manager = m_AssetManager.lock() )
        {
            for ( const auto& [handle, meshAsset] : manager->FindAllByType<Assets::MeshAsset>() )
            {
                Runtime::ResourceRegistry::GetMeshService()->Register( meshAsset );
            }
        }
    }

    void AssetPreloader::PreloadSkyboxes()
    {
        ProcessAssetFiles<SkyboxAsset>( Common::Constants::Path::SKYBOX_PATH, true, SUPPORTED_SKYBOX_EXTENSIONS,
                                        m_AssetManager, AssetPriority::Medium );

        if ( auto manager = m_AssetManager.lock() )
        {
            for ( const auto& [handle, skyboxAsset] : manager->FindAllByType<Assets::SkyboxAsset>() )
            {
                Runtime::ResourceRegistry::GetSkyboxService()->Register( skyboxAsset );
                return;
            }
        }
    }

    void AssetPreloader::PreloadShaders()
    {
        ProcessAssetFiles<ShaderAsset>( Common::Constants::Path::SHADERDIR_PATH, true,
                                        SUPPORTED_SHADERS_EXTENSIONS, m_AssetManager, AssetPriority::Medium );

        if ( auto manager = m_AssetManager.lock() )
        {
            for ( const auto& [handle, shaderAsset] : manager->FindAllByType<Assets::ShaderAsset>() )
            {
                Runtime::ResourceRegistry::GetShaderService()->Register( shaderAsset );
            }
        }
    }

} // namespace Desert::Assets