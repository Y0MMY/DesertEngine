#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult SkyboxRenderer::Initialize( const uint32_t width, const uint32_t height )
    {
        const auto& compositeFramebuffer = m_CompositeFramebuffer.lock();
        if ( !compositeFramebuffer )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "Skybox";

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = compositeFramebuffer;
        m_RenderPass             = Graphic::RenderPass::Create( rpSpec );

        // Pipeline
        m_Shader = Graphic::Shader::Create( "skybox.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = compositeFramebuffer;
        pipeSpec.Shader      = m_Shader;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        return BOOLSUCCESS;
    }

    void SkyboxRenderer::PrepareCamera( const Core::Camera& camera )
    {
        m_ActiveCamera = const_cast<Core::Camera*>( &camera );
    }

    void SkyboxRenderer::ProcessSystem()
    {
        auto& renderer = Renderer::GetInstance();
        renderer.BeginRenderPass( m_RenderPass );

        if ( const auto& material = m_MaterialSkybox.lock() )
        {
            renderer.SubmitFullscreenQuad( m_Pipeline, material->GetMaterialExecutor() );
        }
        renderer.EndRenderPass();
    }

    void SkyboxRenderer::PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material )
    {
        if ( !material )
        {
            return;
        }

        material->UpdateRenderParameters( *m_ActiveCamera );
        m_MaterialSkybox = material;
    }

} // namespace Desert::Graphic::System