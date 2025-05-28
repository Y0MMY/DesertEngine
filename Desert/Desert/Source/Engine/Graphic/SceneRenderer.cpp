#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Core/Application.hpp>
#include <Engine/Core/EngineContext.h>
#include <Engine/Graphic/Materials/PBR/PBRMaterialHelper.hpp>

#include <glm/glm.hpp>

namespace Desert::Graphic
{
    NO_DISCARD Common::BoolResult SceneRenderer::Init()
    {
        uint32_t width  = EngineContext::GetInstance().GetCurrentWindowWidth();
        uint32_t height = EngineContext::GetInstance().GetCurrentWindowHeight();

        // ==================== Skybox Pass ====================
        {
            auto&                      skybox    = m_SceneInfo.Renderdata.Skybox;
            constexpr std::string_view debugName = "Skybox";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = debugName;
            fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );

            skybox.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            skybox.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = skybox.Framebuffer;
            skybox.RenderPass        = Graphic::RenderPass::Create( rpSpec );

            // Pipeline
            skybox.Shader = Graphic::Shader::Create( "skybox.glsl" );

            Graphic::PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" } };
            pipeSpec.Framebuffer = skybox.Framebuffer;
            pipeSpec.Shader      = skybox.Shader;

            skybox.Pipeline = Graphic::Pipeline::Create( pipeSpec );
            skybox.Pipeline->Invalidate();

            // Material
            skybox.Material = Material::Create( std::string( debugName ), skybox.Shader );
            skybox.Material->Invalidate();

            skybox.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        // ==================== Composite Pass ====================
        {
            auto&                      composite = m_SceneInfo.Renderdata.Composite;
            constexpr std::string_view debugName = "SceneComposite";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = debugName;
            fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::BGRA8F );

            composite.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            composite.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = composite.Framebuffer;
            composite.RenderPass     = Graphic::RenderPass::Create( rpSpec );

            // Pipeline
            composite.Shader = Graphic::Shader::Create( "SceneComposite.glsl" );

            Graphic::PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" } };
            pipeSpec.Framebuffer = composite.Framebuffer;
            pipeSpec.Shader      = composite.Shader;

            composite.Pipeline = Graphic::Pipeline::Create( pipeSpec );
            composite.Pipeline->Invalidate();

            composite.Material = Material::Create( std::string( debugName ), composite.Shader );
            composite.Material->Invalidate();

            composite.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        // ==================== Geometry Pass ====================
        {
            auto&                      geometry  = m_SceneInfo.Renderdata.Geometry;
            constexpr std::string_view debugName = "SceneGeometry";

            // Framebuffer
            FramebufferSpecification fbSpec;
            fbSpec.DebugName = debugName;
            fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );

            geometry.Framebuffer = Graphic::Framebuffer::Create( fbSpec );
            geometry.Framebuffer->Resize( width, height );

            // RenderPass
            RenderPassSpecification rpSpec;
            rpSpec.DebugName         = debugName;
            rpSpec.TargetFramebuffer = geometry.Framebuffer;

            // Pipeline
            PipelineSpecification pipeSpec;
            pipeSpec.DebugName   = debugName;
            pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" },
                                     { Graphic::ShaderDataType::Float3, "a_Normal" },
                                     { Graphic::ShaderDataType::Float3, "a_Tangent" },
                                     { Graphic::ShaderDataType::Float3, "a_Bitangent" },
                                     { Graphic::ShaderDataType::Float2, "a_TextureCoord" } };
            pipeSpec.Renderpass  = RenderPass::Create( rpSpec );
            pipeSpec.Shader      = Graphic::Shader::Create( "StaticPBR.glsl" );
            pipeSpec.Framebuffer = geometry.Framebuffer;
            pipeSpec.Shader      = pipeSpec.Shader;

            geometry.Pipeline = Pipeline::Create( pipeSpec );
            geometry.Pipeline->Invalidate();

            geometry.Material = Material::Create( std::string( debugName ), geometry.Shader );
            geometry.Material->Invalidate();

            geometry.UBManager = UniformBufferManager::Create( debugName, pipeSpec.Shader );
        }

        m_SceneInfo.EnvironmentData = EnvironmentManager::Create( "HDR/env.hdr" );

        return BOOLSUCCESS;
    }

    NO_DISCARD Common::BoolResult SceneRenderer::BeginScene( const std::shared_ptr<Core::Scene>& scene,
                                                             const Core::Camera&                 camera )
    {
        m_SceneInfo.ActiveScene  = scene;
        m_SceneInfo.ActiveCamera = const_cast<Core::Camera*>( &camera );

        auto& renderer = Renderer::GetInstance();
        return renderer.BeginFrame();
    }

    NO_DISCARD Common::BoolResult SceneRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();

        GeometryRenderPass();
        CompositeRenderPass();

        return renderer.EndFrame();
    }

    void SceneRenderer::OnEvent( Common::Event& e )
    {
        Common::EventManager eventManager( e );
        eventManager.Notify<Common::EventWindowResize>( [this]( Common::EventWindowResize& e )
                                                        { return this->OnWindowResize( e ); } );
    }

    bool SceneRenderer::OnWindowResize( Common::EventWindowResize& e )
    {
        auto&                                              renderer = Renderer::GetInstance();
        std::vector<std::shared_ptr<Graphic::Framebuffer>> framebuffers;
        framebuffers.push_back( m_SceneInfo.Renderdata.Skybox.Framebuffer );
        renderer.ResizeWindowEvent( e.width, e.height, framebuffers );
        return false;
    }

    void SceneRenderer::CompositeRenderPass()
    {

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        auto& renderer = Renderer::GetInstance();

        renderer.BeginSwapChainRenderPass();
        m_SceneInfo.Renderdata.Composite.Material->SetImage2D(
             "u_GeometryTexture", m_SceneInfo.Renderdata.Skybox.Framebuffer->GetColorAttachmentImage() );

        renderer.SubmitFullscreenQuad( m_SceneInfo.Renderdata.Composite.Pipeline,
                                       m_SceneInfo.Renderdata.Composite.Material );
        renderer.EndRenderPass();
    }

    void SceneRenderer::GeometryRenderPass()
    {
        auto& renderer = Renderer::GetInstance();

        renderer.BeginRenderPass( m_SceneInfo.Renderdata.Skybox.RenderPass );

        struct
        {
            glm::mat4 m1;
            glm::mat4 m2;
        } camera;

        camera.m1 = m_SceneInfo.ActiveCamera->GetProjectionMatrix();
        camera.m2 = m_SceneInfo.ActiveCamera->GetViewMatrix();

        const auto& cameraUBRes = m_SceneInfo.Renderdata.Skybox.UBManager->GetUniformBuffer( "camera" );
        const auto& cameraUB    = cameraUBRes.GetValue();
        cameraUB->RT_SetData( &camera, 128, 0 );
        m_SceneInfo.Renderdata.Skybox.Material->AddUniformToOverride( cameraUB );

        renderer.SubmitFullscreenQuad( m_SceneInfo.Renderdata.Skybox.Pipeline,
                                       m_SceneInfo.Renderdata.Skybox.Material );

        for ( const auto& meshIno : m_SceneInfo.Renderdata.MeshInfo )
        {
            const MaterialHelper::PBRMaterial PBRTechnique( m_SceneInfo.Renderdata.Geometry.Material );
            const MaterialHelper::PBRUniforms pbr;

            const auto& vp =
                 m_SceneInfo.ActiveCamera->GetProjectionMatrix() * m_SceneInfo.ActiveCamera->GetViewMatrix();
            m_SceneInfo.Renderdata.Geometry.Material->PushConstant( &vp, sizeof( vp ) );

            renderer.RenderMesh( m_SceneInfo.Renderdata.Geometry.Pipeline, meshIno.Mesh, PBRTechnique );
        }

        renderer.EndRenderPass();
    }

    void SceneRenderer::RenderMesh( const std::shared_ptr<Mesh>& mesh )
    {
        m_SceneInfo.Renderdata.MeshInfo.push_back( { mesh } );
    }

    const Environment SceneRenderer::CreateEnvironment( const Common::Filepath& filepath )
    {
        return EnvironmentManager::Create( filepath );
    }

    void SceneRenderer::SetEnvironment( const Environment& environment )
    {
        m_SceneInfo.EnvironmentData = environment;

        m_SceneInfo.Renderdata.Skybox.Material->SetImageCube( "samplerCubeMap",
                                                              m_SceneInfo.EnvironmentData.RadianceMap );
    }

    const Environment& SceneRenderer::GetEnvironment()
    {
        return m_SceneInfo.EnvironmentData;
    }

} // namespace Desert::Graphic