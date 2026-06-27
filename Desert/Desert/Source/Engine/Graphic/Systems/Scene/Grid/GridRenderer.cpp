#include "GridRenderer.hpp"

#include <Engine/Graphic/SceneRenderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic::System
{
    Common::BoolResultStr GridRenderer::Initialize()
    {
        const auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Grid" );
        if ( !shader )
            return Common::MakeError( "GridRenderer: missing shader 'Grid'" );

        GraphicsPipelineSpecification spec;
        spec.DebugName         = "GridPipeline";
        spec.Shader            = shader;
        spec.Framebuffer       = m_TargetFramebuffer.lock();
        spec.DepthTestEnabled  = true;  // occluded by opaque geometry
        spec.DepthWriteEnabled = false; // overlay; don't write depth
        spec.DepthCompareOp    = CompareOp::Less;
        spec.CullMode          = CullMode::None;
        spec.BlendEnable       = true;  // alpha-composite the lines over the scene

        m_Pipeline = GraphicsPipeline::Create( spec );
        m_Pipeline->Invalidate();

        m_Material = std::make_unique<MaterialGrid>();
        if ( !m_Pipeline )
            return Common::MakeError( "GridRenderer: failed to create pipeline" );
        return BOOLSUCCESS;
    }

    void GridRenderer::Shutdown()
    {
        m_Pipeline.reset();
        m_Material.reset();
    }

    void GridRenderer::RegisterPasses( RenderGraphBuilder& builder )
    {
        auto targetFb = m_TargetFramebuffer.lock();
        if ( !targetFb || !m_Pipeline )
            return;

        // After Geometry, into the scene framebuffer (merges into the open render pass -> depth available,
        // no clear). Transparency phase so it alpha-blends over the lit scene before the post chain.
        builder.AddPass(
             "GridPass", RenderPhase::Transparency,
             [this]()
             {
                 if ( !m_ShowGrid )
                     return;
                 const auto* camera = m_SceneRenderer->GetMainCamera();
                 if ( !camera )
                     return;

                 m_Material->Update( camera );
                 Renderer::GetInstance().SubmitFullscreenQuad( m_Pipeline.get(),
                                                               m_Material->GetMaterialExecutor() );
             },
             m_Pipeline->GetSpecification(), targetFb, { RenderPassDependency( RenderPhase::Geometry ) } );
    }
} // namespace Desert::Graphic::System
