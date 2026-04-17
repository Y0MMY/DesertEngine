#include "AssetPreloader.hpp"

#include "Shader/ShaderAsset.hpp"
#include "Mesh/StaticMeshAsset.hpp"
#include "Mesh/SkinnedMeshAsset.hpp"
#include "Mesh/AnimationAsset.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 1> SUPPORTED_SKINNED_MESH_EXTENSIONS = { ".skmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_STATIC_MESH_EXTENSIONS  = { ".stmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKELETON_EXTENSIONS     = { ".skeleton" };
    constexpr std::array<std::string_view, 1> SUPPORTED_MATERIAL_EXTENSIONS     = { ".mat" };
    constexpr std::array<std::string_view, 1> SUPPORTED_ANIMATION_EXTENSIONS    = { ".anim" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKYBOX_EXTENSIONS       = { ".hdr" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SHADERS_EXTENSIONS      = { ".shader" };

    AssetPreloader::AssetPreloader( const std::shared_ptr<AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    void AssetPreloader::PreloadAllAssets()
    {
        PreloadShaders();
        PreloadMeshes();
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
        ProcessAssetFiles<StaticMeshAsset>( Common::Constants::Path::MESH_PATH_COOKED, false,
                                            SUPPORTED_STATIC_MESH_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<AnimationAsset>( Common::Constants::Path::MESH_PATH_COOKED, false,
                                           SUPPORTED_ANIMATION_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<SkeletonAsset>( Common::Constants::Path::MESH_PATH_COOKED, false,
                                          SUPPORTED_SKELETON_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<PBRMaterialAsset>( Common::Constants::Path::MESH_PATH_COOKED, false,
                                             SUPPORTED_MATERIAL_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<SkinnedMeshAsset>( Common::Constants::Path::MESH_PATH_COOKED, false,
                                             SUPPORTED_SKINNED_MESH_EXTENSIONS, m_AssetManager,
                                             AssetPriority::Low );

        /* ProcessAssetFiles<MaterialAsset>( Common::Constants::Path::MESH_PATH, false, SUPPORTED_MESH_EXTENSIONS,
                                          m_AssetManager, AssetPriority::Low );*/

        if ( auto manager = m_AssetManager.lock() )
        {
            for ( const auto& [handle, meshAsset] : manager->FindAllByType<Assets::MeshAsset>() )
            {
                Runtime::ResourceRegistry::GetMeshService()->Register( meshAsset );
            }

            for ( const auto& [handle, materialAsset] : manager->FindAllByType<Assets::MaterialAsset>() )
            {
                Runtime::ResourceRegistry::GetMaterialService()->Register( materialAsset );
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
                //  m_RuntimeServices->Skyboxes().Register( skyboxAsset );
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