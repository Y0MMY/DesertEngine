#include "TonemapRenderer.hpp"

namespace Desert::Graphic::System
{
    Common::BoolResult TonemapRenderer::Initialize( const uint32_t width, const uint32_t height )
    {
        const auto& compositeFramebuffer = m_CompositeFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "SceneToneMap";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = debugName;
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( width, height );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;
        m_RenderPass             = Graphic::RenderPass::Create( rpSpec );

        // Pipeline
        m_Shader = Graphic::Shader::Create( "SceneComposite.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        m_MaterialTonemap = std::make_unique<MaterialTonemap>();

        return BOOLSUCCESS;
    }

    void TonemapRenderer::ProcessSystem()
    {
        const auto& framebuffer = m_CompositeFramebuffer.lock();
        if ( !framebuffer )
        {
            LOG_ERROR( "The framebuffer for `TonemapRenderer::ProcessSystem` was destroyed or wasn't set up" );
            return;
        }

        auto& renderer = Renderer::GetInstance();
        renderer.BeginRenderPass( m_RenderPass );

        m_MaterialTonemap->UpdateRenderParameters( framebuffer->GetColorAttachmentImage() );

        renderer.SubmitFullscreenQuad( m_Pipeline, m_MaterialTonemap->GetMaterialExecutor() );
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic::System