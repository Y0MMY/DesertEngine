#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResultStr SkyboxRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_TargetFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "Skybox";

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = compositeFramebuffer;

        // Pipeline
        m_Shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Skybox" );

        Graphic::GraphicsPipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = compositeFramebuffer;
        pipeSpec.Shader      = m_Shader;

        pipeSpec.CullMode          = CullMode::None;
        pipeSpec.DepthTestEnabled  = false;
        pipeSpec.DepthWriteEnabled = false;

        m_Pipeline = Graphic::GraphicsPipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        // Procedural sky: same fullscreen-quad pass/target, but the engine-generated atmosphere shader.
        m_ProceduralShader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "ProceduralSky" );
        if ( m_ProceduralShader )
        {
            Graphic::GraphicsPipelineSpecification skySpec;
            skySpec.DebugName         = "ProceduralSky";
            skySpec.Framebuffer       = compositeFramebuffer;
            skySpec.Shader            = m_ProceduralShader;
            skySpec.CullMode          = CullMode::None;
            skySpec.DepthTestEnabled  = false;
            skySpec.DepthWriteEnabled = false;

            m_ProceduralPipeline = Graphic::GraphicsPipeline::Create( skySpec );
            m_ProceduralPipeline->Invalidate();

            m_ProceduralMaterial = std::make_shared<MaterialProceduralSky>();
        }

        return BOOLSUCCESS;
    }

    void SkyboxRenderer::PrepareCamera( Core::Camera* camera )
    {
        m_ActiveCamera = camera;
    }

    void SkyboxRenderer::PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material )
    {
        // Only record the material here. This can run from the skybox-load command (ExecuteAll) BEFORE
        // BeginScene/PrepareCamera, so the active camera may not exist yet — the camera-dependent bind
        // is deferred to Render(), which always runs with a valid camera.
        if ( !material )
        {
            return;
        }

        m_MaterialSkybox = material;
    }

    void SkyboxRenderer::EnsureProceduralEnvironment()
    {
        if ( !m_UseProceduralSky || !m_EnvDirty )
            return;

        // The bake runs immediate compute dispatches; idle the device first (mirrors the editor's
        // skybox-swap path) since we're recreating GPU images that prior frames may have referenced.
        Renderer::GetInstance().WaitDeviceIdle();

        Environment baked = EnvironmentManager::CreateProcedural( m_SunDir, m_SunIntensity, m_SunDiskRadius );
        if ( !baked )
        {
            m_EnvDirty = false; // bake failed (e.g. shader missing) — keep prior env; user can retry via Bake.
            return;
        }

        const Environment previous = m_ProceduralEnv;
        m_ProceduralEnv            = baked;

        // Release the previous baked cubes (the image service owns them until unregistered).
        if ( previous )
        {
            auto* imageService = Runtime::ResourceRegistry::GetImageService();
            imageService->Unregister( previous.RadianceMap );
            imageService->Unregister( previous.IrradianceMap );
            imageService->Unregister( previous.PreFilteredMap );
        }

        m_BakedSunDir = glm::normalize( m_SunDir );
        m_EnvDirty    = false;
    }

    void SkyboxRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb )
            return;

        builder.AddPass( "SkyboxPass", RenderPhase::Sky, [this]() { Render(); },
                         m_Pipeline ? m_Pipeline->GetSpecification() : GraphicsPipelineSpecification{}, targetFb );
    }

    void SkyboxRenderer::Render()
    {
        auto& renderer = Renderer::GetInstance();

        // Engine-generated procedural atmosphere (no HDR asset needed).
        if ( m_UseProceduralSky && m_ProceduralPipeline && m_ProceduralMaterial && m_ActiveCamera )
        {
            m_ProceduralMaterial->Update( m_ActiveCamera, m_SunDir, m_SunIntensity, m_SunDiskRadius, m_Clouds );
            renderer.SubmitFullscreenQuad( m_ProceduralPipeline.get(),
                                           m_ProceduralMaterial->GetMaterialExecutor() );
            return;
        }

        if ( const auto& material = m_MaterialSkybox.lock() )
        {
            if ( m_ActiveCamera )
                material->Bind( { m_ActiveCamera } );
            renderer.SubmitFullscreenQuad( m_Pipeline.get(), material->GetMaterialExecutor() );
        }
    }

} // namespace Desert::Graphic::System
