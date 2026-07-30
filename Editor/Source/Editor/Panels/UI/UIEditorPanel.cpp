#include "UIEditorPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/UI/UILayout.hpp>

#include <ImGui/imgui.h>

#include <cstring>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        ImU32 Col( const glm::vec3& c, float a = 1.0f )
        {
            return ImGui::ColorConvertFloat4ToU32( ImVec4( c.r, c.g, c.b, a ) );
        }

        // Recursively draw one UI element + its children. `parent` is the element's parent rect in SCREEN
        // pixels; `scale` maps design px -> screen px (for font sizing).
        void DrawElement( entt::registry& reg, entt::entity e, const UI::Rect& parent, float scale, ImDrawList* dl,
                          bool interactive )
        {
            UI::Rect rect = parent;
            if ( reg.has<ECS::UILayoutComponent>( e ) )
            {
                const auto& L = reg.get<ECS::UILayoutComponent>( e ).Data;
                rect = UI::ResolveRect( L.AnchorMin, L.AnchorMax, L.OffsetMin, L.OffsetMax, L.CustomMinimumSize,
                                        parent );

                const ImVec2 mn( rect.X, rect.Y );
                const ImVec2 mx( rect.X + rect.W, rect.Y + rect.H );

                // Button = a tinted, hover-reactive rounded rect.
                if ( reg.has<ECS::UIButtonComponent>( e ) )
                {
                    const auto&  b     = reg.get<ECS::UIButtonComponent>( e ).Data;
                    const ImVec2 m     = ImGui::GetMousePos();
                    const bool   hover = interactive && m.x >= mn.x && m.x <= mx.x && m.y >= mn.y && m.y <= mx.y;
                    const bool   down  = hover && ImGui::IsMouseDown( ImGuiMouseButton_Left );
                    const glm::vec3 c  = down ? b.PressedColor : ( hover ? b.HoverColor : b.NormalColor );
                    dl->AddRectFilled( mn, mx, Col( c, 1.0f ), 6.0f );
                }
                else if ( reg.has<ECS::UIPanelComponent>( e ) )
                {
                    const auto& p = reg.get<ECS::UIPanelComponent>( e ).Data;
                    dl->AddRectFilled( mn, mx, Col( p.Color, p.Opacity ), p.CornerRadius );
                }

                // Text label (may sit on top of a panel/button).
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
                        DrawElement( reg, c, rect, scale, dl, interactive );
        }

        // Create a UI child entity (with a UILayout) parented to `parent`, and attach `element` to it.
        template <typename ElementComponent>
        void AddUIChild( ::Desert::Core::Scene& scene, entt::entity parent, const char* name )
        {
            auto& e      = scene.CreateNewEntity( std::string( name ) );
            auto  handle = e.GetHandle();
            e.AddComponent<ECS::UILayoutComponent>();
            e.AddComponent<ElementComponent>();

            auto& reg = scene.GetRegistry();
            if ( !reg.has<ECS::RelationshipComponent>( handle ) )
                reg.emplace<ECS::RelationshipComponent>( handle );
            reg.get<ECS::RelationshipComponent>( handle ).Parent = parent;
            if ( !reg.has<ECS::RelationshipComponent>( parent ) )
                reg.emplace<ECS::RelationshipComponent>( parent );
            reg.get<ECS::RelationshipComponent>( parent ).Children.push_back( handle );
        }
    } // namespace

    UIEditorPanel::UIEditorPanel( const std::shared_ptr<::Desert::Core::Scene>& scene )
         : IPanel( "UI Editor", /*showPanel=*/false ), m_Scene( scene )
    {
    }

    void UIEditorPanel::OnUIRender()
    {
        if ( !m_Scene )
        {
            ImGui::TextDisabled( "No active scene." );
            return;
        }

        auto& reg        = m_Scene->GetRegistry();
        auto  canvasView = reg.view<ECS::UICanvasComponent>();

        // Toolbar.
        if ( canvasView.begin() == canvasView.end() )
        {
            ImGui::TextDisabled( "No UI Canvas in the scene." );
            if ( ImGui::Button( ICON_MDI_PLUS " Create UI Canvas" ) )
            {
                auto& e = m_Scene->CreateNewEntity( "UI Canvas" );
                e.AddComponent<ECS::UICanvasComponent>();
            }
            return;
        }

        entt::entity canvasEntity = *canvasView.begin();
        const auto&  canvasData   = reg.get<ECS::UICanvasComponent>( canvasEntity ).Data;

        if ( ImGui::Button( ICON_MDI_CARD_OUTLINE " + Panel" ) )
            AddUIChild<ECS::UIPanelComponent>( *m_Scene, canvasEntity, "UI Panel" );
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_FORMAT_TEXT " + Text" ) )
            AddUIChild<ECS::UITextComponent2D>( *m_Scene, canvasEntity, "UI Text" );
        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_BUTTON_POINTER " + Button" ) )
            AddUIChild<ECS::UIButtonComponent>( *m_Scene, canvasEntity, "UI Button" );
        ImGui::SameLine();
        ImGui::TextDisabled( "add UI elements, then edit anchors/colour in Details" );
        ImGui::Separator();

        if ( !canvasData.Visible )
        {
            ImGui::TextDisabled( "Canvas is hidden (UI Canvas -> Visible)." );
            return;
        }

        // Letterbox the design resolution into the panel's content region.
        const ImVec2   origin = ImGui::GetCursorScreenPos();
        const ImVec2   avail  = ImGui::GetContentRegionAvail();
        const UI::Rect fit =
             UI::CanvasRect( canvasData.ReferenceWidth, canvasData.ReferenceHeight, avail.x, avail.y );
        const UI::Rect canvasRect{ origin.x + fit.X, origin.y + fit.Y, fit.W, fit.H };
        const float    scale = ( canvasData.ReferenceWidth > 0.0f ) ? fit.W / canvasData.ReferenceWidth : 1.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Canvas backdrop (checkerboard-ish dark) + border.
        dl->AddRectFilled( ImVec2( canvasRect.X, canvasRect.Y ),
                           ImVec2( canvasRect.X + canvasRect.W, canvasRect.Y + canvasRect.H ),
                           IM_COL32( 24, 25, 30, 255 ) );
        dl->AddRect( ImVec2( canvasRect.X, canvasRect.Y ),
                     ImVec2( canvasRect.X + canvasRect.W, canvasRect.Y + canvasRect.H ),
                     IM_COL32( 90, 90, 100, 200 ) );

        const bool interactive = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows );
        if ( reg.has<ECS::RelationshipComponent>( canvasEntity ) )
            for ( auto c : reg.get<ECS::RelationshipComponent>( canvasEntity ).Children )
                if ( reg.valid( c ) )
                    DrawElement( reg, c, canvasRect, scale, dl, interactive );

        ImGui::Dummy( avail );
    }
} // namespace Desert::Editor
