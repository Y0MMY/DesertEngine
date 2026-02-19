#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    void SceneRenderer::Init()
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
            DESERT_VERIFY( false );

        if ( !SP_CAST( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->Initialize() )
            DESERT_VERIFY( false );

        if ( !SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )->Initialize() )
            DESERT_VERIFY( false );

        RebuildRenderGraph();
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::BeginScene( const Desert::Core::Scene& scene )
    {
        const auto& mainCamera   = scene.GetMainCamera().lock();
        m_SceneInfo.ActiveCamera = mainCamera.get();

        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        const auto& meshSystem   = UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] );

        skyboxSystem->PrepareCamera( m_SceneInfo.ActiveCamera );

        const auto& sceneSettings = scene.GetSettings();
        meshSystem->SetOutlineColor( sceneSettings.OutlineColor );
        meshSystem->ToggleOutline( sceneSettings.EnableOutline );
        meshSystem->SetOutlineWidth( sceneSettings.OutlineWidth );
        auto& renderer = Renderer::GetInstance();

        return renderer.BeginFrame();
    }

    void SceneRenderer::OnUpdate( const UpdateInfo& sceneRenderInfo )
    {
        const auto& skyboxSystem = UNIQUE_GET_AS( System::SkyboxRenderer, m_RenderSystems["SkyboxSystem"] );
        // m_DirectionLights        = sceneRenderInfo.DirLights;

        ClearMainFramebuffer();
        ExecuteRenderGraph();
        CompositeRenderPass();
    }

    NO_DISCARD Common::BoolResultStr SceneRenderer::EndScene()
    {
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )->ClearQueues();

        m_PointLight.PointLights.clear();

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

    void SceneRenderer::AddStaticMesh( const std::shared_ptr<Mesh>&              mesh,
                                       const std::shared_ptr<StaticMaterialPBR>& material,
                                       const glm::mat4&                          transform )
    {
        if ( !mesh || !material )
            return;
        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->AddStaticMesh( mesh, material, transform );
    }

    void SceneRenderer::AddSkinnedMesh( const std::shared_ptr<Desert::SkinnedMesh>&         mesh,
                                        const std::shared_ptr<Graphic::SkinnedMaterialPBR>& material,
                                        const glm::mat4& transform, const std::vector<glm::mat4>& boneMatrices )
    {
        if ( !mesh || !material || boneMatrices.empty() )
            return;

        UNIQUE_GET_AS( System::MeshRenderer, m_RenderSystems["MeshSystem"] )
             ->AddSkinnedMesh( mesh, material, transform, boneMatrices );
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

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage()
    {
        return SP_CAST( System::TonemapRenderer, m_RenderSystems["TonemapSystem"] )
             ->GetSystemFramebuffer()
             ->GetColorAttachmentImage();
    }

    void SceneRenderer::AddPointLight( ShaderProtocols::PointLightPayload&& pointLight )
    {
        m_PointLight.PointLights.push_back( std::move( pointLight ) );
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