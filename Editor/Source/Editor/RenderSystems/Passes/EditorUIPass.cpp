#include "EditorUIPass.hpp"

#include <Engine/Graphic/Renderer.hpp>
#include <Engine/UI/UICanvasRenderer2D.hpp>

#include <Editor/Core/Selection/UIPreview.hpp>

#include <Common/Core/Logger.hpp>

namespace Desert::Editor::Render
{
    EditorUIPass::~EditorUIPass()
    {
        if ( const auto scene = m_Scene.lock() )
            scene->UnregisterExternalPass( "EditorUI2D" );
    }

    Common::BoolResultStr EditorUIPass::Install( const std::shared_ptr<::Desert::Core::Scene>& scene )
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

            // UI Preview (Play-in-editor): feed the viewport's pointer/keyboard into the canvas so buttons /
            // toggles / sliders react in the editor. The ViewportPanel wrote this snapshot in viewport-display
            // px; scale it into the framebuffer's px space. Design mode leaves input null (normal authoring).
            auto&       pv = Editor::Core::UIPreview::Get();
            UI::UIInput input;
            std::string clicked;
            const bool  feed = pv.Enabled && pv.HasInput && pv.DisplaySize.x > 0.0f && pv.DisplaySize.y > 0.0f;
            if ( feed )
            {
                input.MousePx       = { pv.MousePx.x * ( w / pv.DisplaySize.x ),
                                        pv.MousePx.y * ( h / pv.DisplaySize.y ) };
                input.MouseDown     = pv.Down;
                input.MouseReleased = pv.Released;
                input.ScrollDelta   = pv.Scroll;
                input.Tab           = pv.Tab;
                input.Submit        = pv.Submit;
                input.Backspace     = pv.Backspace;
                input.TypedText     = pv.TypedText;
            }

            UI::RenderCanvas2D( scene->GetRegistry(), m_Render2D.GetDrawList(), UI::Rect{ 0.0f, 0.0f, w, h },
                                vpPtr, feed ? &input : nullptr, feed ? &clicked : nullptr,
                                feed ? &pv.Focused : nullptr );
            m_Render2D.Flush();

            // A button fired in preview: report it, but DON'T execute scene-load / quit here — that would
            // close/switch the editor. Interactive toggles/sliders/inputs already mutated in the walk.
            if ( feed && !clicked.empty() )
                LOG_INFO( "[UI Preview] button action: {}", clicked );
        };

        scene->RegisterExternalPass( std::move( pass ) );
        return BOOLSUCCESS;
    }
} // namespace Desert::Editor::Render
