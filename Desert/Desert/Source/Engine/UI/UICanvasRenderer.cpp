#include "UICanvasRenderer.hpp"

#include <Engine/ECS/Components.hpp>

#include <ImGui/imgui.h>

#include <cfloat>

namespace Desert::UI
{
    namespace
    {
        namespace ImGui = ::ImGui;

        ImU32 Col( const glm::vec3& c, float a = 1.0f )
        {
            return ImGui::ColorConvertFloat4ToU32( ImVec4( c.r, c.g, c.b, a ) );
        }

        // Recursively resolve + draw one element and its children. `parent` is the parent rect in SCREEN
        // pixels; `scale` maps design px -> screen px (font sizing). A hovered+released button writes its
        // OnClickMessage to *outClicked. This is the SINGLE draw path shared by the editor preview and the
        // in-game runtime, so a canvas looks identical in both.
        void DrawElement( entt::registry& reg, entt::entity e, const Rect& parent, float scale, ImDrawList* dl,
                          bool interactive, std::string* outClicked )
        {
            Rect rect = parent;
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect = ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin, L.OffsetMax, L.CustomMinimumSize,
                                    parent );

                const ImVec2 mn( rect.X, rect.Y );
                const ImVec2 mx( rect.X + rect.W, rect.Y + rect.H );

                if ( reg.has<ECS::UIButtonComponent>( e ) )
                {
                    const auto&  b     = reg.get<ECS::UIButtonComponent>( e ).Data;
                    const ImVec2 m     = ImGui::GetMousePos();
                    const bool   hover = interactive && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
                    const bool   down  = hover && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                    const glm::vec3 c  = down ? b.PressedColor : ( hover ? b.HoverColor : b.NormalColor );
                    dl->AddRectFilled( mn, mx, Col( c, 1.0f ), 6.0f );

                    if ( hover && outClicked && ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                        *outClicked = b.OnClickMessage;
                }
                else if ( reg.has<ECS::UIPanelComponent>( e ) )
                {
                    const auto& p = reg.get<ECS::UIPanelComponent>( e ).Data;
                    dl->AddRectFilled( mn, mx, Col( p.Color, p.Opacity ), p.CornerRadius );
                }

                if ( reg.has<ECS::UITextComponent2D>( e ) )
                {
                    const auto&  t  = reg.get<ECS::UITextComponent2D>( e ).Data;
                    const float  fs = t.FontSize * scale;
                    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA( fs, FLT_MAX, 0.0f, t.Text.c_str() );
                    float        tx = mn.x + 6.0f;
                    if ( t.Align == ECS::UITextAlign::Center )
                        tx = mn.x + ( rect.W - ts.x ) * 0.5f;
                    else if ( t.Align == ECS::UITextAlign::Right )
                        tx = mx.x - ts.x - 6.0f;
                    const float ty = mn.y + ( rect.H - ts.y ) * 0.5f;
                    dl->AddText( ImGui::GetFont(), fs, ImVec2( tx, ty ), Col( t.Color, 1.0f ), t.Text.c_str() );
                }
            }

            if ( reg.has<ECS::RelationshipComponent>( e ) )
                for ( auto c : reg.get<ECS::RelationshipComponent>( e ).Children )
                    if ( reg.valid( c ) )
                        DrawElement( reg, c, rect, scale, dl, interactive, outClicked );
        }
    } // namespace

    bool RenderCanvas( entt::registry& reg, ImDrawList* dl, const Rect& viewportPx, bool interactive,
                       std::string* outClicked )
    {
        auto canvasView = reg.view<ECS::UICanvasComponent>();
        if ( canvasView.begin() == canvasView.end() )
            return false;

        const entt::entity canvasEntity = *canvasView.begin();
        const auto&        canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;
        if ( !canvasData.Visible )
            return false;

        // Letterbox the design resolution into the target viewport, matching the editor preview exactly.
        const Rect fit =
             CanvasRect( canvasData.ReferenceWidth, canvasData.ReferenceHeight, viewportPx.W, viewportPx.H );
        const Rect  canvasRect{ viewportPx.X + fit.X, viewportPx.Y + fit.Y, fit.W, fit.H };
        const float scale = ( canvasData.ReferenceWidth > 0.0f ) ? fit.W / canvasData.ReferenceWidth : 1.0f;

        if ( reg.has<ECS::RelationshipComponent>( canvasEntity ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvasEntity ).Children )
                if ( reg.valid( c ) )
                    DrawElement( reg, c, canvasRect, scale, dl, interactive, outClicked );

        return true;
    }
} // namespace Desert::UI
