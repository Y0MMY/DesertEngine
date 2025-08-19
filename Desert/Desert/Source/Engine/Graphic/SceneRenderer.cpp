#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    enum FixedRenderSystems
    {
        SkyboxSystem  = 0,
        MeshSystem    = 1,
        TonemapSystem = 2
    };

    NO_DISCARD Common::BoolResult SceneRenderer::Init()
    {
        const uint32_t width  = Common::CommonContext::GetInstance().GetCurrentWindowWidth();
        const uint32_t height = Common::CommonContext::GetInstance().GetCurrentWindowHeight();

        m_RenderGraph = std::make_shared<RenderGraph>();

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = "Composite framebuffer";
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );

        m_CompositeFramebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_CompositeFramebuffer->Resize( width, height );

        RegisterSystem<System::SkyboxRenderer>( FixedRenderSystems::SkyboxSystem, m_CompositeFramebuffer,
                                                m_RenderGraph );
        RegisterSystem<System::MeshRenderer>( FixedRenderSystems::MeshSystem, m_CompositeFramebuffer,
                                              m_RenderGraph );
        RegisterSystem<System::TonemapRenderer>( FixedRenderSystems::TonemapSystem, m_CompositeFramebuffer,
                                                 m_RenderGraph );

        if ( !m_FixedRenderSystems[FixedRenderSystems::SkyboxSystem]->Initialize( width, height ) )
            return Common::MakeError( "Failed to initialize SkyboxRenderer system" );

        if ( !m_FixedRenderSystems[FixedRenderSystems::MeshSystem]->Initialize( width, height ) )
            return Common::MakeError( "Failed to initialize MeshRenderer system" );

        if ( !m_FixedRenderSystems[FixedRenderSystems::TonemapSystem]->Initialize( width, height ) )
            return Common::MakeError( "Failed to initialize TonemapRenderer system" );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>&  scene,
                                                             const std::shared_ptr<Core::Camera>& camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = camera;

        const auto& skyboxSystem =
             UNIQUE_GET_AS( System::SkyboxRenderer, m_FixedRenderSystems[FixedRenderSystems::SkyboxSystem] );
        const auto& meshSystem =
             UNIQUE_GET_AS( System::MeshRenderer, m_FixedRenderSystems[FixedRenderSystems::MeshSystem] );

        skyboxSystem->PrepareCamera( camera );

        meshSystem->PrepareFrame( camera, skyboxSystem->GetEnvironment() );
        const auto& sceneSettings = scene->GetSettings();
        meshSystem->SetOutlineColor( sceneSettings.OutlineColor );
        meshSystem->ToggleOutline( sceneSettings.EnableOutline );
        meshSystem->SetOutlineWidth( sceneSettings.OutlineWidth );
        auto& renderer = Renderer::GetInstance();

        return renderer.BeginFrame();
    }

    void SceneRenderer::OnUpdate( const SceneRendererUpdate& sceneRenderInfo )
    {
        const auto& skyboxSystem =
             UNIQUE_GET_AS( System::SkyboxRenderer, m_FixedRenderSystems[FixedRenderSystems::SkyboxSystem] );
        const auto& meshSystem =
             UNIQUE_GET_AS( System::MeshRenderer, m_FixedRenderSystems[FixedRenderSystems::MeshSystem] );

        meshSystem->AddLight( BuildDirectionLight(
             sceneRenderInfo.DirLights ) /*{ sceneRenderInfo.DirLights[0].Direction, {}, 0.0 } */ );

        m_RenderGraph->Execute();

        // m_RenderGraph.Execute();
        CompositeRenderPass();
    }

    NO_DISCARD Common::BoolResult SceneRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();
        return renderer.EndFrame();
    }

    void SceneRenderer::Resize( const uint32_t width, const uint32_t height )
    {
        if ( width == 0 && height == 0 )
            return;
        auto& renderer = Renderer::GetInstance();
        renderer.ResizeWindowEvent( width, height );
        m_CompositeFramebuffer->Resize( width, height );
        m_FixedRenderSystems[FixedRenderSystems::MeshSystem]->GetSystemFramebuffer()->Resize(
             width,
             height ); // TODO: event
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
        UNIQUE_GET_AS( System::MeshRenderer, m_FixedRenderSystems[FixedRenderSystems::MeshSystem] )
             ->AddMesh( { mesh, transform, material } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return {}; // EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const std::shared_ptr<MaterialSkybox>& material )
    {
        UNIQUE_GET_AS( System::SkyboxRenderer, m_FixedRenderSystems[FixedRenderSystems::SkyboxSystem] )
             ->PrepareMaterial( material );
    }

    const Environment& SceneRenderer::GetEnvironment()
    {
        return {}; // m_SkyboxRenderer->GetEnvironment();
    }

    void SceneRenderer::Shutdown()
    {
        for ( const auto& system : m_FixedRenderSystems )
        {
            system->Shutdown();
        }
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage() const
    {
        return m_FixedRenderSystems[FixedRenderSystems::TonemapSystem]
             ->GetSystemFramebuffer()
             ->GetColorAttachmentImage();
    }

    const glm::vec3 SceneRenderer::BuildDirectionLight( const std::vector<DirectionLight>& dirLights )
    {
        if ( !dirLights.empty() )
        {
            return dirLights[0].Direction;
        }

        return glm::vec3( 0.0f );
    }

    void SceneRenderer::RegisterExternalPass( std::string&& name, std::function<void()> execute,
                                              std::shared_ptr<RenderPass>&& renderPass )
    {
        //  m_RenderGraph.AddPass( std::move( name ), std::move( execute ), std::move( renderPass ) );
    }

} // namespace Desert::Graphic