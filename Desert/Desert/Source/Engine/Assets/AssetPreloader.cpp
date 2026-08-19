#include "AssetPreloader.hpp"
#include <Common/Utilities/VFS.hpp>
#include <unordered_set>

#include "Shader/ShaderAsset.hpp"
#include "Mesh/StaticMeshAsset.hpp"
#include "Mesh/SkinnedMeshAsset.hpp"
#include "Mesh/AnimationAsset.hpp"
#include "TextureAsset.hpp"
#include "CloudNoiseVolumeAsset.hpp"
#include "CloudTypeAsset.hpp"

namespace Desert::Assets
{
    constexpr std::array<std::string_view, 1> SUPPORTED_SKINNED_MESH_EXTENSIONS = { ".skmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_STATIC_MESH_EXTENSIONS  = { ".stmesh" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKELETON_EXTENSIONS     = { ".skeleton" };
    // (now written by import too). Both register as SurfaceMaterialAsset.
    constexpr std::array<std::string_view, 1> SUPPORTED_MATERIAL_EXTENSIONS     = { ".demat" };
    constexpr std::array<std::string_view, 1> SUPPORTED_ANIMATION_EXTENSIONS    = { ".anim" };
    constexpr std::array<std::string_view, 1> SUPPORTED_TEXTURE_EXTENSIONS      = { ".tex" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SKYBOX_EXTENSIONS       = { ".hdr" };
    constexpr std::array<std::string_view, 1> SUPPORTED_SHADERS_EXTENSIONS      = { ".shader" };
    constexpr std::array<std::string_view, 1> SUPPORTED_CLOUD_NOISE_EXTENSIONS  = { ".dcnv" };
    constexpr std::array<std::string_view, 1> SUPPORTED_CLOUD_TYPE_EXTENSIONS   = { ".decloudtype" };

    AssetPreloader::AssetPreloader( const std::shared_ptr<AssetManager>& assetManager )
         : m_AssetManager( assetManager )
    {
    }

    void AssetPreloader::PreloadAllAssets()
    {
        PreloadShaders();
        PreloadMeshes();
        PreloadSkyboxes();
        PreloadCloudNoiseVolumes();
        PreloadCloudTypes();
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

            // Candidates = the loose files on disk PLUS everything a mounted .dpak holds under this
            // root (packaged game: the disk dirs typically do not exist at all). Deduplicated by
            // normalized path — a loose file overrides its pak twin.
            std::vector<fs::path>           candidates;
            std::unordered_set<std::string> seen;
            auto push = [&]( const fs::path& p )
            {
                if ( seen.insert( p.lexically_normal().generic_string() ).second )
                    candidates.push_back( p );
            };

            std::error_code ec;
            if ( fs::exists( rootPath, ec ) ) // tolerate missing cooked dirs (clean/from-scratch project)
            {
                for ( const auto& entry : fs::recursive_directory_iterator( rootPath, ec ) )
                    if ( entry.is_regular_file() )
                        push( entry.path() );
            }
            for ( const auto& packed : Common::Utils::VFS::ListFiles( rootPath ) )
                push( packed );

            for ( const auto& candidate : candidates )
            {
                std::string ext = candidate.extension().string();
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
                    const Common::Filepath path = candidate;
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

        // Materials are editable CONTENT (the project's Materials/ dir): imported (per-mesh
        // subfolders) and editor-created both land here, in the unified .demat format.
        ProcessAssetFiles<SurfaceMaterialAsset>( Common::Constants::Path::MATERIAL_PATH,
                                                 SUPPORTED_MATERIAL_EXTENSIONS, m_AssetManager,
                                                 AssetPriority::Low );

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

    void AssetPreloader::PreloadCloudNoiseVolumes()
    {
        // Loaded eagerly, unlike meshes: a volume is 8 MiB of bytes with no parse to speak of, and the
        // renderer needs its contents on the first frame the component asks for it. Deferring would buy a
        // stall exactly where the sky first appears.
        ProcessAssetFiles<CloudNoiseVolumeAsset>( Common::Constants::Path::CLOUD_NOISE_PATH,
                                                  SUPPORTED_CLOUD_NOISE_EXTENSIONS, m_AssetManager,
                                                  AssetPriority::Medium );

        if ( auto manager = m_AssetManager.lock() )
        {
            auto* service = Runtime::ResourceRegistry::GetCloudNoiseService();
            for ( const auto& [handle, volumeAsset] : manager->FindAllByType<Assets::CloudNoiseVolumeAsset>() )
            {
                if ( const auto result = service->Register( volumeAsset ); !result )
                {
                    LOG_ERROR( "[Clouds] Noise volume '{}' could not be uploaded: {}",
                               volumeAsset->GetMetadata().Filepath.string(), result.GetError() );
                    continue;
                }

                // The default is chosen by FILE NAME, and it is a project-owned file rather than something
                // compiled in: a project that ships its own CloudNoise_Default.dcnv replaces the engine's
                // without touching code, which is the same way every other built-in default here works.
                if ( volumeAsset->GetMetadata().Filepath.filename().string() == kCloudNoiseDefaultVolumeName )
                    service->SetDefault( handle );
            }
        }
    }

    void AssetPreloader::PreloadCloudTypes()
    {
        // Loaded eagerly like the volumes, and for a smaller version of the same reason: a type is a few
        // hundred bytes of JSON, the renderer needs its numbers on the first frame the layer asks for
        // them, and a scene that names one must find it already there rather than resolve to the built-in
        // default for the first second of every session.
        //
        // THERE IS NO "DEFAULT TYPE" FILE to nominate here, unlike the volumes. The empty slot resolves to
        // Assets::CloudTypeDefaultShape — twelve numbers compiled in — because a type costs nothing to
        // synthesise where a 128^3 volume costs ten seconds, and because the sky of a project that has
        // deleted every file in Clouds/Types must still be the sky it was.
        ProcessAssetFiles<CloudTypeAsset>( Common::Constants::Path::CLOUD_TYPE_PATH,
                                           SUPPORTED_CLOUD_TYPE_EXTENSIONS, m_AssetManager,
                                           AssetPriority::Medium );

        if ( auto manager = m_AssetManager.lock() )
        {
            auto* service = Runtime::ResourceRegistry::GetCloudTypeService();
            for ( const auto& [handle, typeAsset] : manager->FindAllByType<Assets::CloudTypeAsset>() )
            {
                if ( const auto result = service->Register( typeAsset ); !result )
                    LOG_ERROR( "[Clouds] Cloud type '{}' could not be registered: {}",
                               typeAsset->GetMetadata().Filepath.string(), result.GetError() );
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