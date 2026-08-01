#include "EditorUIPass.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/UI/UICanvasRenderer2D.hpp>

namespace Desert::Editor::Render
{
    EditorUIPass::~EditorUIPass()
    {
        if ( const auto scene = m_Scene.lock() )
            scene->UnregisterExternalPass( "EditorUI2D" );
    }

    Common::BoolResultStr EditorUIPass::Install( const std::shared_ptr<Core::Scene>& scene )
    {
        m_Scene = scene;

        if ( const auto result = m_Render2D.Init( scene->GetTargetFramebuffer() ); !result )
            return Common::MakeError( "EditorUIPass: " + result.GetError() );

        Graphic::ExternalPassSpecification pass;
        pass.Name                  = "EditorUI2D";
        pass.Phase                 = Graphic::RenderPhase::UI;
        pass.Dependencies          = { Graphic::RenderPassDependency( Graphic::RenderPhase::Geometry ) };
        pass.PipelineSpecification = m_Render2D.GetPipeline()->GetSpecification();
        pass.Execute               = [this]( const Graphic::ExternalPassContext& ctx )
        {
            const auto scene = m_Scene.lock();
            if ( !scene || !ctx.Target )
                return;

            const float w = static_cast<float>( ctx.Target->GetFramebufferWidth() );
            const float h = static_cast<float>( ctx.Target->GetFramebufferHeight() );

            // Camera view-proj for world-space canvases (screen-space ignores it).
            glm::mat4        vp( 1.0f );
            const glm::mat4* vpPtr = nullptr;
            if ( ctx.Camera )
            {
                vp    = ctx.Camera->GetProjectionMatrix() * ctx.Camera->GetViewMatrix();
                vpPtr = &vp;
            }

            m_Render2D.BeginFrame( { 0.0f, 0.0f, w, h } );
            UI::RenderCanvas2D( scene->GetRegistry(), m_Render2D.GetDrawList(), UI::Rect{ 0.0f, 0.0f, w, h },
                                vpPtr );
            m_Render2D.Flush();
        };

        scene->RegisterExternalPass( std::move( pass ) );
        return BOOLSUCCESS;
    }
} // namespace Desert::Editor::Render
