#include "AutoExposureRenderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResultStr AutoExposureRenderer::Initialize()
    {
        const auto makeFb = [&]( const std::string& name )
        {
            FramebufferSpecification spec;
            spec.DebugName = name;
            spec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA32F );
            auto fb = Graphic::Framebuffer::Create( spec );
            fb->Resize( 1, 1 );
            return fb;
        };
        m_LumFB[0] = makeFb( "AutoExposure_Lum0" );
        m_LumFB[1] = makeFb( "AutoExposure_Lum1" );

        const auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "AutoExposure" );
        if ( !shader )
            return Common::MakeError( "AutoExposureRenderer: missing shader 'AutoExposure'" );

        GraphicsPipelineSpecification spec;
        spec.DebugName         = "AutoExposurePipeline";
        spec.Shader            = shader;
        spec.Framebuffer       = m_LumFB[0];
        spec.DepthTestEnabled  = false;
        spec.DepthWriteEnabled = false;
        spec.CullMode          = CullMode::None;
        m_Pipeline             = GraphicsPipeline::Create( spec );
        m_Pipeline->Invalidate();

        m_Material = std::make_unique<MaterialAutoExposure>();

        return BOOLSUCCESS;
    }

    void AutoExposureRenderer::Shutdown()
    {
        m_Pipeline.reset();
        m_Material.reset();
        m_LumFB[0].reset();
        m_LumFB[1].reset();
    }

    void AutoExposureRenderer::Execute()
    {
        const auto& scene = m_TargetFramebuffer.lock();
        if ( !scene )
            return;

        const int prev  = m_ReadIndex;       // last frame's adapted luminance
        const int write = 1 - m_ReadIndex;   // we render the new value here

        MaterialAutoExposure::Params params{ 0.016f, m_AdaptSpeed, m_MinLuma, m_MaxLuma };
        m_Material->Bind( scene->GetColorAttachmentImage().get(),
                          m_LumFB[prev]->GetColorAttachmentImage().get(), params );

        auto& renderer = Renderer::GetInstance();
        auto  renderPass = RenderPass::Create( {
             .TargetFramebuffer = m_LumFB[write],
             .DebugName         = "AutoExposurePass",
        } );
        renderer.BeginRenderPass( renderPass.get(), true );
        renderer.SubmitFullscreenQuad( m_Pipeline.get(), m_Material->GetMaterialExecutor() );
        renderer.EndRenderPass();

        m_ReadIndex = write; // the freshly written buffer is now the latest
    }
} // namespace Desert::Graphic::System
