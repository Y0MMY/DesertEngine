#include <Engine/Runtime/AssetHotReload.hpp>

#include <Engine/Assets/AssetManager.hpp>
#include <Engine/Assets/Mesh/SurfaceMaterialAsset.hpp>
#include <Engine/Assets/Shader/ShaderAsset.hpp>
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
        m_FirstScan = false;
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

            std::error_code ec;
            const auto      mtime = std::filesystem::last_write_time( path, ec );
            if ( ec )
                continue;

            const std::string key = path.generic_string();
            auto              it  = m_KnownTimes.find( key );
            if ( it == m_KnownTimes.end() )
            {
                m_KnownTimes[key] = mtime;
                continue;
            }
            if ( it->second == mtime || m_FirstScan )
            {
                it->second = mtime;
                continue;
            }
            it->second = mtime;

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

            LOG_INFO( "[HotReload] Shader '{}' recompiled", shader->GetName() );

            // Rebuild cached pipelines against the new modules. Renderer-owned specialized
            // pipelines (batched PBR/shadow/etc.) are built once at init and still need a
            // restart to pick the change up.
            if ( scene )
                if ( auto* sceneRenderer = scene->GetSceneRenderer() )
                {
                    Graphic::Renderer::GetInstance().WaitDeviceIdle();
                    sceneRenderer->GetPipelineCache().InvalidateByShader( shader.get() );
                }
        }
    }
} // namespace Desert::Runtime
