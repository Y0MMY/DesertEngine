#include <Engine/Runtime/AssetHotReload.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Shader/ShaderAsset.hpp>
#include <Engine/Assets/CloudNoiseVolumeAsset.hpp>
#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/Graphic/Materials/DataDrivenMaterial.hpp>
#include <Engine/Graphic/Materials/MaterialFactory.hpp>
#include <Engine/Graphic/Materials/Mesh/PBR/StaticMaterialPBR.hpp>
#include <Engine/Graphic/PipelineCache.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Runtime/Services/Material/MaterialService.hpp>
#include <Engine/Runtime/Services/Shader/ShaderService.hpp>
#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>
#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>

namespace Desert::Runtime
{
    namespace
    {
        // Refresh every static/skinned mesh component that references @p materialHandle so
        // MeshECSSystem rebuilds its runtime instances against the new runtime material.
        void RefreshComponentsUsingMaterial( Core::Scene* scene, const Assets::AssetHandle& materialHandle )
        {
            if ( !scene )
                return;
            auto& registry = scene->GetRegistry();

            registry.view<ECS::StaticMeshComponent>().each(
                 [&]( ECS::StaticMeshComponent& mesh )
                 {
                     for ( const auto& slot : mesh.MaterialSlots )
                         if ( slot == materialHandle )
                         {
                             mesh.RuntimeMaterialInstances.clear();
                             mesh.RuntimeSlotPtrs.clear();
                             break;
                         }
                 } );

            registry.view<ECS::SkinnedMeshComponent>().each(
                 [&]( ECS::SkinnedMeshComponent& mesh )
                 {
                     for ( const auto& slot : mesh.MaterialSlots )
                         if ( slot == materialHandle )
                         {
                             mesh.RuntimeMaterialInstances.clear();
                             break;
                         }
                 } );
        }
    } // namespace

    void AssetHotReload::Tick( const Common::Timestep& ts, Assets::AssetManager& assetManager,
                               Core::Scene* scene )
    {
        m_Accum += ts.GetSeconds();
        if ( m_Accum < kPollInterval )
            return;
        m_Accum = 0.0f;

        PollMaterials( assetManager, scene );
        PollShaders( assetManager, scene );
        PollCloudNoiseVolumes( assetManager );
        m_FirstScan = false;
    }

    void AssetHotReload::PollCloudNoiseVolumes( Assets::AssetManager& assetManager )
    {
        auto* service = ResourceRegistry::GetCloudNoiseService();

        for ( const auto& [handle, asset] : assetManager.FindAllByType<Assets::CloudNoiseVolumeAsset>() )
        {
            if ( !asset )
                continue;
            const auto& path = asset->GetMetadata().Filepath;

            std::error_code ec;
            const auto      mtime = std::filesystem::last_write_time( path, ec );
            if ( ec )
                continue; // deleted/missing — leave the in-memory volume alone

            const std::string key = path.generic_string();
            auto              it  = m_KnownTimes.find( key );
            if ( it == m_KnownTimes.end() )
            {
                m_KnownTimes[key] = mtime;
                continue; // first sighting — baseline only
            }
            if ( it->second == mtime || m_FirstScan )
            {
                it->second = mtime;
                continue;
            }
            it->second = mtime;

            // A FAILED RE-READ LEAVES THE OLD VOLUME BOUND, deliberately. The panel writes a volume with a
            // single truncating stream write, so a poll that lands mid-write sees a short file; refusing it
            // and keeping the bytes that are already on the device costs one poll interval, where accepting
            // a half-file would put a seam across the sky and then never mention it again.
            if ( const auto reloaded = asset->Load(); !reloaded )
            {
                LOG_ERROR( "[HotReload] Cloud noise volume '{}' could not be re-read: {}", key,
                           reloaded.GetError() );
                continue;
            }

            if ( const auto uploaded = service->Register( asset ); !uploaded )
            {
                LOG_ERROR( "[HotReload] Cloud noise volume '{}' was re-read but not uploaded: {}", key,
                           uploaded.GetError() );
                continue;
            }

            // Nothing else to notify. VolumetricCloudRenderer asks the service for its volume every frame
            // rather than caching it, so the next frame binds the new image without anyone telling it.
            LOG_INFO( "[HotReload] Cloud noise volume '{}' reloaded — the next frame marches the new one.", key );
        }
    }

    void AssetHotReload::PollMaterials( Assets::AssetManager& assetManager, Core::Scene* scene )
    {
        auto* materialService = ResourceRegistry::GetMaterialService();

        for ( const auto& [handle, asset] : assetManager.FindAllByType<Assets::SurfaceMaterialAsset>() )
        {
            if ( !asset )
                continue;
            const auto& path = asset->GetMetadata().Filepath;

            std::error_code ec;
            const auto      mtime = std::filesystem::last_write_time( path, ec );
            if ( ec )
                continue; // deleted/missing — leave the in-memory asset alone

            const std::string key = path.generic_string();
            auto              it  = m_KnownTimes.find( key );
            if ( it == m_KnownTimes.end() )
            {
                m_KnownTimes[key] = mtime;
                continue; // first sighting — baseline only
            }
            if ( it->second == mtime || m_FirstScan )
            {
                it->second = mtime;
                continue;
            }
            it->second = mtime;

            const bool wasCustom = asset->Data().UsesCustomShader();
            const auto oldShader = asset->GetShaderName();

            if ( const auto res = asset->Load(); !res )
            {
                LOG_ERROR( "[HotReload] Failed to re-parse material '{}': {}", key, res.GetError() );
                continue;
            }

            LOG_INFO( "[HotReload] Material '{}' reloaded", key );

            if ( !materialService )
                continue;

            auto* runtime = materialService->Get( handle );

            const bool classMatches =
                 runtime && ( asset->Data().UsesCustomShader()
                                   ? dynamic_cast<Graphic::DataDrivenMaterial*>( runtime ) != nullptr
                                   : dynamic_cast<Graphic::StaticMaterialPBR*>( runtime ) != nullptr );
            const bool sameShader = classMatches && asset->GetShaderName() == oldShader;

            if ( sameShader && !asset->Data().UsesCustomShader() )
            {
                Graphic::MaterialFactory::ApplyPBRAsset(
                     *static_cast<Graphic::StaticMaterialPBR*>( runtime ), *asset );
            }
            else if ( sameShader )
            {
                Graphic::MaterialFactory::ApplyShaderAsset(
                     *static_cast<Graphic::DataDrivenMaterial*>( runtime ), *asset );
            }
            else
            {
                // Shader (or material class) changed — rebuild the runtime material and refresh
                // every component using it.
                materialService->Invalidate( handle );
                RefreshComponentsUsingMaterial( scene, handle );
            }
        }
    }

    bool AssetHotReload::TouchWatched( const std::filesystem::path& path )
    {
        std::error_code ec;
        const auto      mtime = std::filesystem::last_write_time( path, ec );
        if ( ec )
            return false; // deleted/missing — leave the in-memory version alone

        const std::string key = path.generic_string();
        auto              it  = m_KnownTimes.find( key );
        if ( it == m_KnownTimes.end() )
        {
            m_KnownTimes[key] = mtime; // first sighting — baseline only
            return false;
        }

        const bool changed = it->second != mtime;
        it->second         = mtime;
        return changed;
    }

    void AssetHotReload::PollShaders( Assets::AssetManager& assetManager, Core::Scene* scene )
    {
        auto* shaderService = ResourceRegistry::GetShaderService();
        if ( !shaderService )
            return;

        for ( const auto& [handle, asset] : assetManager.FindAllByType<Assets::ShaderAsset>() )
        {
            if ( !asset )
                continue;
            const auto& path = asset->GetMetadata().Filepath;

            // A shader is its .shader file AND every .glslh that file pulls in — the same closure the
            // SPIR-V cache key hashes, so the two cannot disagree about what a shader is made of.
            // Watching only the .shader was a real hole: a shared header could be edited and nothing
            // recompiled until the next restart, which is the same silent staleness an under-specified
            // cache key produces, arriving through the other door.
            bool changed = TouchWatched( path );
            for ( const auto& include : Core::CollectShaderIncludes( asset->GetShaderContent(), path ) )
            {
                changed = TouchWatched( include ) || changed;
            }

            if ( !changed || m_FirstScan )
                continue;

            const std::string key = path.generic_string();

            if ( const auto res = asset->Load(); !res )
            {
                LOG_ERROR( "[HotReload] Failed to re-read shader '{}': {}", key, res.GetError() );
                continue;
            }

            // Pre-validate the DSL structure: the preprocessor treats a malformed file as a fatal
            // engine error (DESERT_VERIFY), which is right at startup but must not kill a live
            // editor over a half-saved file. GLSL errors inside the stages fail gracefully later.
            if ( Core::Preprocess::DShaderParser::IsDShader( asset->GetShaderContent() ) )
            {
                if ( auto parsed = Core::Preprocess::DShaderParser::Parse( asset->GetShaderContent() );
                     !parsed.IsSuccess() )
                {
                    LOG_ERROR( "[HotReload] Shader '{}': {} — keeping the previous version", key,
                               parsed.GetError() );
                    continue;
                }
            }

            auto shader = shaderService->Get( handle );
            if ( !shader )
                continue;

            if ( const auto res = shader->Reload(); !res )
            {
                // Compile errors land here (and in the Logs panel). Existing pipelines keep the
                // previous working code; save again to retry.
                LOG_ERROR( "[HotReload] Shader '{}' failed to compile: {}", shader->GetName(),
                           res.GetError() );
                continue;
            }

            // Renderer-owned pipelines (the batched PBR/shadow set, every compute pipeline, the
            // fog apply) are built once at init and keep the code they were built with until
            // a restart. That is now merely STALE and no longer unsafe: the pipeline holds strong
            // references to the descriptor set layouts it was built from, so recompiling under it
            // cannot leave it bound to a layout that has been destroyed (see
            // VulkanDescriptorSetLayout.hpp). Said on every recompile, because "the shader did not
            // change anything" is otherwise indistinguishable from "the shader did not compile".
            LOG_INFO( "[HotReload] Shader '{}' recompiled. Graph pipelines pick it up next frame; "
                      "renderer-owned pipelines (compute, batched PBR/shadow, fog apply) keep "
                      "the previous code until the editor is restarted.",
                      shader->GetName() );

            // Rebuild cached pipelines against the new modules.
            if ( scene )
                if ( auto* sceneRenderer = scene->GetSceneRenderer() )
                {
                    Graphic::Renderer::GetInstance().WaitDeviceIdle();
                    sceneRenderer->GetPipelineCache().InvalidateByShader( shader.get() );
                }
        }
    }
} // namespace Desert::Runtime
