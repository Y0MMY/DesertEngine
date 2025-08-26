#include "TonemapRenderer.hpp"

namespace Desert::Graphic::System
{
    Common::BoolResult TonemapRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_CompositeFramebuffer.lock();
        const auto& renderGraph          = m_RenderGraph.lock();
        if ( !compositeFramebuffer || !renderGraph )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "SceneToneMap";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = debugName;
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( m_Width, m_Height);

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;

        // Pipeline
        m_Shader = Graphic::Shader::Create( "SceneComposite.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        m_MaterialTonemap = std::make_unique<MaterialTonemap>();

        renderGraph->AddPass(
             "TonemapPass",
             [this]()
             {
                 const auto& framebuffer =
                      m_CompositeFramebuffer.lock(); // We call lock internally to avoid cyclic dependencies.
                 if ( !framebuffer )
                 {
                     LOG_ERROR(
                          "The framebuffer for `TonemapRenderer::ProcessSystem` was destroyed or wasn't set up" );
                     return;
                 }

                 auto& renderer = Renderer::GetInstance();
                 m_MaterialTonemap->UpdateRenderParameters( framebuffer->GetColorAttachmentImage() );
                 renderer.SubmitFullscreenQuad( m_Pipeline, m_MaterialTonemap->GetMaterialExecutor() );
             },
             RenderPass::Create( rpSpec ) );
        return BOOLSUCCESS;
    }
} // namespace Desert::Graphic::System