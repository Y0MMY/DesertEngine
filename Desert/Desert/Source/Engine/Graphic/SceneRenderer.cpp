#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    NO_DISCARD Common::BoolResult SceneRenderer::Init()
    {
        const uint32_t width  = Common::CommonContext::GetInstance().GetCurrentWindowWidth();
        const uint32_t height = Common::CommonContext::GetInstance().GetCurrentWindowHeight();

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = "Composite framebuffer";
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );

        m_CompositeFramebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_CompositeFramebuffer->Resize( width, height );

        m_SkyboxRenderer  = std::make_unique<System::SkyboxRenderer>();
        m_MeshRenderer    = std::make_unique<System::MeshRenderer>();
        m_TonemapRenderer = std::make_unique<System::TonemapRenderer>();

        if ( !m_SkyboxRenderer->Init( width, height, m_CompositeFramebuffer ) )
            return Common::MakeError( "Failed to initialize SkyboxRenderer system" );

        if ( !m_MeshRenderer->Init( width, height, m_CompositeFramebuffer ) )
            return Common::MakeError( "Failed to initialize MeshRenderer system" );

        if ( !m_TonemapRenderer->Init( width, height ) )
            return Common::MakeError( "Failed to initialize TonemapRenderer system" );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>& scene,
                                                             const Core::Camera&                 camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = const_cast<Core::Camera*>( &camera );
        m_SkyboxRenderer->BeginScene( camera );
        m_MeshRenderer->BeginScene( camera, m_SkyboxRenderer->GetEnvironment() );
        const auto& sceneSettings = scene->GetSettings();
        m_MeshRenderer->SetOutlineColor( sceneSettings.OutlineColor );
        m_MeshRenderer->ToggleOutline( sceneSettings.EnableOutline );
        m_MeshRenderer->SetOutlineWidth( sceneSettings.OutlineWidth );
        auto& renderer = Renderer::GetInstance();
        return renderer.BeginFrame();
    }

    void SceneRenderer::OnUpdate( const SceneRendererUpdate& sceneRenderInfo )
    {
        m_MeshRenderer->SubmitLight( BuildDirectionLight(
             sceneRenderInfo.DirLights ) /*{ sceneRenderInfo.DirLights[0].Direction, {}, 0.0 } */ );

        m_SkyboxRenderer->EndScene();
        m_MeshRenderer->EndScene();
        m_TonemapRenderer->Process(
             m_CompositeFramebuffer ); // NOTE: m_CompositeFramebuffer stores all scene image data (skybox, mesh, etc)
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
        m_MeshRenderer->GetFramebuffer()->Resize( width, height );
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
        m_MeshRenderer->Submit( { mesh, transform, material } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return {}; // EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const std::shared_ptr<MaterialSkybox>& material )
    {
        m_SkyboxRenderer->Submit( material );
    }

    const Environment& SceneRenderer::GetEnvironment()
    {
        return {}; // m_SkyboxRenderer->GetEnvironment();
    }

    void SceneRenderer::Shutdown()
    {
        m_SkyboxRenderer->Shutdown();
        m_MeshRenderer->Shutdown();
        m_TonemapRenderer->Shutdown();
    }

    const std::shared_ptr<Desert::Graphic::Image2D> SceneRenderer::GetFinalImage() const
    {
        return m_TonemapRenderer->GetOutputImage();
    }

    const glm::vec3 SceneRenderer::BuildDirectionLight( const std::vector<DirectionLight>& dirLights )
    {
        if ( !dirLights.empty() )
        {
            return dirLights[0].Direction;
        }

        return glm::vec3( 0.0f );
    }

} // namespace Desert::Graphic