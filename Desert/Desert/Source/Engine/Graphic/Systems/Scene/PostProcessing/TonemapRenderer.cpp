#include "TonemapRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResultStr TonemapRenderer::Initialize()
    {
        const auto& targetFramebuffer = m_TargetFramebuffer.lock();
        if ( !targetFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "SceneToneMap";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = debugName;
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( targetFramebuffer->GetFramebufferWidth(),
                               targetFramebuffer->GetFramebufferHeight() );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;

        // Pipeline
        m_Shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "SceneComposite" );

        Graphic::GraphicsPipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::GraphicsPipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        m_MaterialTonemap = std::make_unique<MaterialTonemap>();

        return BOOLSUCCESS;
    }

    void TonemapRenderer::Execute()
    {
        auto& renderer = Renderer::GetInstance();

        auto renderPass = RenderPass::Create( {
             .TargetFramebuffer = m_Framebuffer,
             .DebugName         = "TonemapPass",
        } );

        renderer.BeginRenderPass( renderPass.get() );
        Render();
        renderer.EndRenderPass();
    }

    void TonemapRenderer::Resize( uint32_t width, uint32_t height )
    {
        if ( m_Framebuffer )
            m_Framebuffer->Resize( width, height );
    }

    void TonemapRenderer::Render()
    {
        const auto& framebuffer =
             m_TargetFramebuffer.lock(); // We call lock internally to avoid cyclic dependencies.
        if ( !framebuffer )
        {
            LOG_ERROR( "The framebuffer for `TonemapRenderer::ProcessSystem` was destroyed or wasn't set up" );
            return;
        }

        auto& renderer = Renderer::GetInstance();
        m_MaterialTonemap->Bind( framebuffer->GetColorAttachmentImage() );
        renderer.SubmitFullscreenQuad( m_Pipeline.get(), m_MaterialTonemap->GetMaterialExecutor() );
    }
} // namespace Desert::Graphic::System