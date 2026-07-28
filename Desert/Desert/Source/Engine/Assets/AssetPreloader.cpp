#include "AssetPreloader.hpp"

#include "Shader/ShaderAsset.hpp"
#include "Mesh/StaticMeshAsset.hpp"
#include "Mesh/SkinnedMeshAsset.hpp"
#include "Mesh/AnimationAsset.hpp"
#include "TextureAsset.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 1> SUPPORTED_SKINNED_MESH_EXTENSIONS = { ".skmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_STATIC_MESH_EXTENSIONS  = { ".stmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKELETON_EXTENSIONS     = { ".skeleton" };
    // ".mat" = legacy cooker output (read-compat via SurfaceMaterialAsset::Load); ".demat" = unified flat format
    // (now written by import too). Both register as SurfaceMaterialAsset.
    constexpr std::array<std::string_view, 2> SUPPORTED_MATERIAL_EXTENSIONS     = { ".mat", ".demat" };
    constexpr std::array<std::string_view, 1> SUPPORTED_ANIMATION_EXTENSIONS    = { ".anim" };
    constexpr std::array<std::string_view, 1> SUPPORTED_TEXTURE_EXTENSIONS      = { ".tex" };
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
        void ProcessAssetFiles( const std::filesystem::path& rootPath,
                                const Extensions&                  supportedExtensions,
                                const std::weak_ptr<AssetManager>& assetManager, AssetPriority priority,
                                Args&&... args )
        {
            namespace fs = std::filesystem;

            std::error_code ec;
            if ( !fs::exists( rootPath, ec ) ) // tolerate missing cooked dirs (clean/from-scratch project)
                return;

            for ( const auto& entry : fs::recursive_directory_iterator( rootPath, ec ) )
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
                    // Assets are ALWAYS registered under their full (project-rooted) path. The old
                    // "strip the Textures/ prefix for skyboxes" hack forced every consumer to re-glue
                    // the prefix back (SceneEnvironment did) — path composition belongs to the asset
                    // layer, not to engine draw code.
                    const Common::Filepath path = entry.path();
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
        // Meshes are scanned as UNPARSED shells (loadAfterCreate=false): the handle is path-derived in the
        // ctor, so the big .stmesh parse + GPU build are deferred to the first Get (lazy). Textures/materials
        // are cheap to parse (small metadata) so they load now to expose their stored handle / external id,
        // but their GPU build is still deferred (RegisterAsset, below).
        ProcessAssetFiles<StaticMeshAsset>( Common::Constants::Path::MESH_PATH_COOKED,
                                            SUPPORTED_STATIC_MESH_EXTENSIONS, m_AssetManager, AssetPriority::Low,
                                            /*loadAfterCreate=*/false );

        ProcessAssetFiles<TextureAsset>( Common::Constants::Path::TEXTURE_PATH_COOKED,
                                         SUPPORTED_TEXTURE_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<AnimationAsset>( Common::Constants::Path::MESH_PATH_COOKED,
                                           SUPPORTED_ANIMATION_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<SkeletonAsset>( Common::Constants::Path::MESH_PATH_COOKED,
                                          SUPPORTED_SKELETON_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        // Materials are editable CONTENT (Resources/Assets/Materials/...): imported (per-mesh subfolders) and
        // editor-created both land here. Also scan the cooked mesh tree for back-compat with any legacy
        // ".mat"/".demat" that older imports wrote there.
        ProcessAssetFiles<SurfaceMaterialAsset>( Common::Constants::Path::MATERIAL_PATH,
                                             SUPPORTED_MATERIAL_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<SurfaceMaterialAsset>( Common::Constants::Path::MESH_PATH_COOKED,
                                             SUPPORTED_MATERIAL_EXTENSIONS, m_AssetManager, AssetPriority::Low );

        ProcessAssetFiles<SkinnedMeshAsset>( Common::Constants::Path::MESH_PATH_COOKED,
                                             SUPPORTED_SKINNED_MESH_EXTENSIONS, m_AssetManager,
                                             AssetPriority::Low, /*loadAfterCreate=*/false );

        /* ProcessAssetFiles<MaterialAsset>( Common::Constants::Path::MESH_PATH, SUPPORTED_MESH_EXTENSIONS,
                                          m_AssetManager, AssetPriority::Low );*/

        if ( auto manager = m_AssetManager.lock() )
        {
            // Register SHELLS only — the GPU build (texture upload / mesh buffers / material instance) is
            // deferred to the first Get (lazy, cascades from a spawned entity). Textures/materials are
            // loaded first (cheap metadata) so their stored handle / external id is known for the map key.
            for ( const auto& [handle, textureAsset] : manager->FindAllByType<Assets::TextureAsset>() )
            {
                if ( !textureAsset->IsReadyForUse() )
                    textureAsset->Load();
                Runtime::ResourceRegistry::GetTextureService()->RegisterAsset( textureAsset );
            }

            for ( const auto& [handle, meshAsset] : manager->FindAllByType<Assets::MeshAsset>() )
            {
                Runtime::ResourceRegistry::GetMeshService()->RegisterAsset( meshAsset ); // unparsed shell
            }

            for ( const auto& [handle, materialAsset] : manager->FindAllByType<Assets::MaterialAsset>() )
            {
                if ( !materialAsset->IsReadyForUse() )
                    materialAsset->Load();
                Runtime::ResourceRegistry::GetMaterialService()->RegisterAsset( materialAsset );
            }
        }
    }

    void AssetPreloader::ReloadCooked()
    {
        // Re-process cooked files (new ones get created) and re-register meshes/textures/materials.
        // Register reloads texture pixels from source and rebuilds runtime materials against the new
        // images (textures are re-registered before materials in PreloadMeshes, so no dangling images).
        PreloadMeshes();
    }

    void AssetPreloader::PreloadSkyboxes()
    {
        ProcessAssetFiles<SkyboxAsset>( Common::Constants::Path::SKYBOX_PATH, SUPPORTED_SKYBOX_EXTENSIONS,
                                        m_AssetManager, AssetPriority::Medium );

        if ( auto manager = m_AssetManager.lock() )
        {
            for ( const auto& [handle, skyboxAsset] : manager->FindAllByType<Assets::SkyboxAsset>() )
            {
                // Eagerly build + cache each skybox's IBL (radiance / irradiance / prefilter compute) at
                // load — Register() constructs the MaterialSkybox which runs the compute once and caches
                // it. Then selecting an HDR skybox in the editor is instant (no per-select compute stall).
                // Runs after PreloadShaders (the compute shaders must be registered first).
                Runtime::ResourceRegistry::GetSkyboxService()->Register( skyboxAsset );
            }
        }
    }

    void AssetPreloader::PreloadShaders()
    {
        ProcessAssetFiles<ShaderAsset>( Common::Constants::Path::SHADERDIR_PATH,
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