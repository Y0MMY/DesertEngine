#include "EditorGridPass.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Editor::Render
{
    EditorGridPass::~EditorGridPass()
    {
        if ( const auto scene = m_Scene.lock() )
            scene->UnregisterExternalPass( "EditorGrid" );
    }

    Common::BoolResultStr EditorGridPass::Install( const std::shared_ptr<Core::Scene>& scene )
    {
        m_Scene = scene;

        const auto shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( "Grid" );
        if ( !shader )
            return Common::MakeError( "EditorGridPass: missing shader 'Grid'" );

        Graphic::GraphicsPipelineSpecification spec;
        spec.DebugName         = "EditorGridPipeline";
        spec.Shader            = shader;
        spec.Framebuffer       = scene->GetTargetFramebuffer();
        spec.DepthTestEnabled  = true;  // occluded by opaque geometry
        spec.DepthWriteEnabled = false; // overlay; don't write depth
        spec.DepthCompareOp    = Graphic::DepthCompare::Closer;
        spec.CullMode          = Graphic::CullMode::None;
        spec.BlendEnable       = true; // alpha-composite the lines over the scene

        m_Pipeline = Graphic::GraphicsPipeline::Create( spec );
        if ( !m_Pipeline )
            return Common::MakeError( "EditorGridPass: failed to create pipeline" );
        m_Pipeline->Invalidate();

        m_Material = std::make_unique<Graphic::MaterialGrid>();

        Graphic::ExternalPassSpecification pass;
        pass.Name                  = "EditorGrid";
        pass.Phase                 = Graphic::RenderPhase::Transparency;
        pass.Dependencies          = { Graphic::RenderPassDependency( Graphic::RenderPhase::Geometry ) };
        pass.PipelineSpecification = m_Pipeline->GetSpecification();
        pass.Execute               = [this]( const Graphic::ExternalPassContext& ctx )
        {
            const auto scene = m_Scene.lock();
            if ( !scene || ctx.ScenePlaying || !scene->GetSettings().ShowGrid || !ctx.Camera )
                return;

            m_Material->Update( ctx.Camera );
            Graphic::Renderer::GetInstance().SubmitFullscreenQuad( m_Pipeline.get(),
                                                                   m_Material->GetMaterialExecutor() );
        };

        scene->RegisterExternalPass( std::move( pass ) );
        return BOOLSUCCESS;
    }
} // namespace Desert::Editor::Render
