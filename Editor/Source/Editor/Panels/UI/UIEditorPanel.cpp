#include "UIEditorPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/Selection/SelectionManager.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>
#include <Engine/UI/UILayout.hpp>
#include <Engine/UI/UICanvasRenderer.hpp>

#include <ImGui/imgui.h>

#include <cstring>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
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
        if ( ImGui::Button( ICON_MDI_VIEW_GRID " + Layout Group" ) )
            AddUIChild<ECS::UILayoutGroupComponent>( *m_Scene, canvasEntity, "UI Layout Group" );
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

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Canvas backdrop (checkerboard-ish dark) + border.
        dl->AddRectFilled( ImVec2( canvasRect.X, canvasRect.Y ),
                           ImVec2( canvasRect.X + canvasRect.W, canvasRect.Y + canvasRect.H ),
                           IM_COL32( 24, 25, 30, 255 ) );
        dl->AddRect( ImVec2( canvasRect.X, canvasRect.Y ),
                     ImVec2( canvasRect.X + canvasRect.W, canvasRect.Y + canvasRect.H ),
                     IM_COL32( 90, 90, 100, 200 ) );

        // Same draw path as the in-game runtime (Engine/UI/UICanvasRenderer) — the preview is pixel-identical
        // to what ships. Hover tinting on buttons is enabled, but clicks are inert here (outClicked = nullptr)
        // so previewing a menu never fires its actions.
        const bool interactive = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows );
        UI::RenderCanvas( reg, dl, UI::Rect{ origin.x, origin.y, avail.x, avail.y }, interactive,
                          /*outClicked=*/nullptr );

        ImGui::Dummy( avail );
    }
} // namespace Desert::Editor
