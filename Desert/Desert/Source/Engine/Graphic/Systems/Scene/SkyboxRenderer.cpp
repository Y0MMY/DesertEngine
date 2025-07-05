#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult SkyboxRenderer::Init( const uint32_t width, const uint32_t height )
    {
        constexpr std::string_view debugName = "Skybox";

        // Framebuffer
        FramebufferSpecification fbSpec;
        fbSpec.DebugName = debugName;
        fbSpec.Attachments.Attachments.push_back( Core::Formats::ImageFormat::RGBA8F );

        m_Framebuffer = Graphic::Framebuffer::Create( fbSpec );
        m_Framebuffer->Resize( width, height );

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = m_Framebuffer;
        m_RenderPass             = Graphic::RenderPass::Create( rpSpec );

        // Pipeline
        m_Shader = Graphic::Shader::Create( "skybox.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Layout      = { { Graphic::ShaderDataType::Float3, "a_Position" } };
        pipeSpec.Framebuffer = m_Framebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return BOOLSUCCESS;
    }

    void SkyboxRenderer::BeginScene( const Core::Camera& camera )
    {
        m_ActiveCamera = const_cast<Core::Camera*>( &camera );
    }

    void SkyboxRenderer::EndScene()
    {
        auto& renderer = Renderer::GetInstance();
        renderer.BeginRenderPass( m_RenderPass );

        if ( const auto& material = m_MaterialSkybox.lock() )
        {
            material->UpdateRenderParameters( *m_ActiveCamera );
            renderer.SubmitFullscreenQuad( m_Pipeline, material->GetMaterial() );
        }
        renderer.EndRenderPass();
    }

} // namespace Desert::Graphic::System