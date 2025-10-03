#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    NO_DISCARD Common::BoolResultStr SceneRenderer::Init()
    {
        const uint32_t width  = EngineContext::GetInstance().GetCurrentWindow()->GetWidth();
        const uint32_t height = EngineContext::GetInstance().GetCurrentWindow()->GetHeight();

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = "Composite framebuffer";
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::DEPTH24STENCIL8 );

        m_TargetFramebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_TargetFramebuffer->Resize( width, height );

        RegisterSystem<System::SkyboxRenderer>( "SkyboxSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        RegisterSystem<System::MeshRenderer>( "MeshSystem", this, m_TargetFramebuffer, m_RenderGraphBuilder );
        RegisterSystem<System::TonemapRenderer>( "TonemapSystem", this, m_TargetFramebuffer,
                                                 m_RenderGraphBuilder );

        if ( !SP_CAST( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->Initialize() )
            return Common::MakeError( "Failed to initialize SkyboxRenderer system" );

        if ( !SP_CAST( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->Initialize() )
            return Common::MakeError( "Failed to initialize MeshRenderer system" );

        if ( !SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Initialize() )
            return Common::MakeError( "Failed to initialize TonemapRenderer system" );

        RebuildRenderGraph();

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>&  scene,
                                                             const std::shared_ptr<Core::Camera>& camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = camera;

        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        const auto& meshSystem   = UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] );

        skyboxSystem->PrepareCamera( camera );

        const auto& sceneSettings = scene->GetSettings();
        meshSystem->SetOutlineColor( sceneSettings.OutlineColor );
        meshSystem->ToggleOutline( sceneSettings.EnableOutline );
        meshSystem->SetOutlineWidth( sceneSettings.OutlineWidth );
        auto& renderer = Renderer::GetInstance();

        return renderer.BeginFrame();
    }

    void SceneRenderer::OnUpdate( const SceneRendererUpdate& sceneRenderInfo )
    {
        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        m_DirectionLights        = sceneRenderInfo.DirLights;

        ClearMainFramebuffer();
        ExecuteRenderGraph();
        CompositeRenderPass();
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::EndScene()
    {
        m_MeshRenderData.clear();
        m_PointLight.clear();

        auto& renderer = Renderer::GetInstance();
        return renderer.EndFrame();
    }

    void SceneRenderer::Resize( const uint32_t width, const uint32_t height )
    {
        if ( width == 0 && height == 0 )
            return;
        auto& renderer = Renderer::GetInstance();
        renderer.ResizeWindowEvent( width, height );
        m_TargetFramebuffer->Resize( width, height );
    }

    // NOTE: if you use rendering without imgui, you may get a black screen! you should start by setting
    // CompositePass!
    void SceneRenderer::CompositeRenderPass()
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto& renderer = Renderer::GetInstance();

        // renderer.BeginSwapChainRenderPass();
        // renderer.EndRenderPass();
    }

    void SceneRenderer::AddToRenderMeshList( const std::shared_ptr<Mesh>&        mesh,
                                             const std::shared_ptr<MaterialPBR>& material,
                                             const glm::mat4&                    transform )
    {
        m_MeshRenderData.emplace_back( mesh, transform, material );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return {}; // EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const std::shared_ptr<MaterialSkybox>& material )
    {
        UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->PrepareMaterial( material );
    }

    const std::optional<Environment>& SceneRenderer::GetEnvironment()
    {
        return UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] )->GetEnvironment();
    }

    void SceneRenderer::Shutdown()
    {
        /*for ( const auto& system : m_FixedRenderSystems )
        {
            system->Shutdown();
        }*/
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage()
    {
        return SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->GetSystemFramebuffer()
             ->GetColorAttachmentImage();
    }

    void SceneRenderer::AddPointLight( PointLight&& pointLight )
    {
        m_PointLight.push_back( std::move( pointLight ) );
    }

    void SceneRenderer::RegisterRenderSystem( const std::string& name, std::shared_ptr<IRenderSystem> system )
    {
        m_RenderSystems[name] = system;
        RebuildRenderGraph();
    }

    void SceneRenderer::UnregisterRenderSystem( const std::string& name )
    {
        m_RenderSystems.erase( name );
        RebuildRenderGraph();
    }

    void SceneRenderer::RebuildRenderGraph()
    {
        m_RenderGraphBuilder.Clear();

        for ( auto& [name, system] : m_RenderSystems )
        {
            system->RegisterPasses( m_RenderGraphBuilder );
        }

        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::DepthPrePass, RenderPhase::Geometry );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Geometry, RenderPhase::Lighting );
        m_RenderGraphBuilder.AddPhaseDependency( RenderPhase::Lighting, RenderPhase::PostProcess );

        if ( !m_RenderGraphBuilder.Build() )
        {
            LOG_ERROR( "Failed to build render graph" );
        }
    }

    void SceneRenderer::ExecuteRenderGraph()
    {
        const auto& sortedPasses = m_RenderGraphBuilder.GetSortedPasses();

        auto& renderer = Renderer::GetInstance();
        for ( const auto& pass : sortedPasses )
        {
            auto renderPass = RenderPass::Create( {
                 .TargetFramebuffer = pass.TargetFramebuffer,
                 .DebugName         = pass.Name,
            } );

            renderer.BeginRenderPass( renderPass );

            pass.ExecuteFunc();

            renderer.EndRenderPass();
        }
    }

    void SceneRenderer::ClearMainFramebuffer()
    {
        auto& renderer = Renderer::GetInstance();

        static auto clearRenderPass = RenderPass::Create( {
             .TargetFramebuffer = m_TargetFramebuffer,
             .DebugName         = "ClearTargetFramebuffer",
        } );

        renderer.BeginRenderPass( clearRenderPass, true );
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic