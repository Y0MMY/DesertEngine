#include "SkyboxRenderer.hpp"
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/FallbackTextures.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResult SkyboxRenderer::Initialize()
    {
        const auto& compositeFramebuffer = m_TargetFramebuffer.lock();
        const auto& renderGraph          = m_RenderGraph.lock();
        if ( !compositeFramebuffer || !renderGraph )
        {
            DESERT_VERIFY( false );
        }

        constexpr std::string_view debugName = "Skybox";

        // RenderPass
        RenderPassSpecification rpSpec;
        rpSpec.DebugName         = debugName;
        rpSpec.TargetFramebuffer = compositeFramebuffer;

        // Pipeline
        m_Shader = Graphic::Shader::Create( "skybox.glsl" );

        Graphic::PipelineSpecification pipeSpec;
        pipeSpec.DebugName   = debugName;
        pipeSpec.Framebuffer = compositeFramebuffer;
        pipeSpec.Shader      = m_Shader;

        pipeSpec.CullMode          = CullMode::None;
        pipeSpec.DepthTestEnabled  = false;
        pipeSpec.DepthWriteEnabled = false;

        m_Pipeline = Graphic::Pipeline::Create( pipeSpec );
        m_Pipeline->Invalidate();

        renderGraph->AddPass(
             "SkyboxPass",
             [this]()
             {
                 auto& renderer = Renderer::GetInstance();
                 if ( const auto& material = m_MaterialSkybox.lock() )
                 {
                     renderer.SubmitFullscreenQuad( m_Pipeline, material->GetMaterialExecutor() );
                 }
             },
             RenderPass::Create( rpSpec ) );

        return BOOLSUCCESS;
    }

    void SkyboxRenderer::PrepareCamera( const std::shared_ptr<Core::Camera>& camera )
    {
        m_ActiveCamera = camera;
    }

    void SkyboxRenderer::PrepareMaterial( const std::shared_ptr<MaterialSkybox>& material )
    {
        const auto& camera = m_ActiveCamera.lock();
        if ( !camera )
        {
            DESERT_VERIFY( false );
        }

        if ( !material )
        {
            return;
        }

        material->Bind( { camera } );
        m_MaterialSkybox = material;
    }

} // namespace Desert::Graphic::System