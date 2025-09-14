#include "TonemapRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult TonemapRenderer::Initialize()
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
        m_Shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "SceneComposite.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        m_MaterialTonemap = std::make_unique<MaterialTonemap>();

        return BOOLSUCCESS;
    }

    void TonemapRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        builder.AddPass( "TonemapPass", RenderPhase::PostProcess, [this]() { Render(); },
                         m_Pipeline ? m_Pipeline->GetSpecification() : PipelineSpecification{}, m_Framebuffer,
                         { RenderPassDependency( RenderPhase::Debug ) } );
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
        renderer.SubmitFullscreenQuad( m_Pipeline, m_MaterialTonemap->GetMaterialExecutor() );
    }
} // namespace Desert::Graphic::System