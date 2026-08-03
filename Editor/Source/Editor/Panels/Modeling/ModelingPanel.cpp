#include "ModelingPanel.hpp"

#include <Editor/Core/Selection/ModelingState.hpp>
#include <Editor/Core/Selection/ViewportMode.hpp>

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <ImGui/imgui.h>

#include <algorithm>

namespace Desert::Editor
{
    ModelingPanel::ModelingPanel( const std::shared_ptr<Desert::Core::Scene>& scene )
         : IPanel( "Modeling" ), m_Scene( scene )
    {
    }

    void ModelingPanel::OnUIRender()
    {
        using MS  = Core::ModelingState;
        auto& ms  = MS::Get();
        auto  sel = ImVec4( 0.20f, 0.55f, 0.95f, 1.0f ); // active highlight (matches the viewport toolbar)

        // --- Left category rail (icon buttons; only Create is populated) ---
        struct Cat
        {
            const char* Icon;
            const char* Name;
        };
        const Cat cats[] = { { ICON_MDI_SHAPE_PLUS, "Create" },   { ICON_MDI_CURSOR_DEFAULT, "Select" },
                             { ICON_MDI_AXIS_ARROW, "XForm" },    { ICON_MDI_GESTURE, "Deform" },
                             { ICON_MDI_VECTOR_SQUARE, "Model" }, { ICON_MDI_CUBE_SCAN, "Mesh" },
                             { ICON_MDI_CUBE_OUTLINE, "Voxel" },  { ICON_MDI_PALETTE, "Bake" } };

        ImGui::BeginChild( "##modeling_cats", ImVec2( 66.0f, 0.0f ), true );
        for ( int i = 0; i < static_cast<int>( IM_ARRAYSIZE( cats ) ); ++i )
        {
            const bool active = i == m_Category;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, sel );
            char label[64];
            std::snprintf( label, sizeof( label ), "%s\n%s", cats[i].Icon, cats[i].Name );
            if ( ImGui::Button( label, ImVec2( -1.0f, 44.0f ) ) )
                m_Category = i;
            if ( active )
                ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Right content: tool grid + tool properties ---
        ImGui::BeginChild( "##modeling_content", ImVec2( 0.0f, 0.0f ), false );

        // Model category (index 4): PolyEdit — face select + push/pull on the selected mesh.
        if ( m_Category == 4 )
        {
            const bool active = ms.ActiveTool == MS::Tool::PolyEdit;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, sel );
            if ( ImGui::Button( ICON_MDI_VECTOR_SQUARE "  PolyEdit", ImVec2( -1.0f, 30.0f ) ) )
            {
                ms.ActiveTool = MS::Tool::PolyEdit;
                Core::ViewportMode::Set( Core::EditorMode::Modeling );
            }
            if ( active )
                ImGui::PopStyleColor();
            ImGui::Separator();
            if ( active )
            {
                ImGui::TextUnformatted( "PolyEdit" );
                ImGui::Spacing();
                ImGui::TextDisabled( "Select a mesh (e.g. a CubeGrid blockout)" );
                ImGui::TextDisabled( "in Select mode first, then:" );
                ImGui::TextDisabled( "LMB a face -> highlights green" );
                ImGui::TextDisabled( "LMB-drag the face -> push / pull" );
                ImGui::Spacing();
                ImGui::TextDisabled( "Extrude / Bevel / Inset: coming next." );
            }
            else
            {
                ImGui::TextDisabled( "Pick PolyEdit to edit a mesh's faces." );
            }
            ImGui::EndChild();
            return;
        }

        if ( m_Category != 0 )
        {
            ImGui::TextDisabled( "This category is not implemented yet." );
            ImGui::EndChild();
            return;
        }

        // Create-category tool grid (2 columns). Only CubeGrid is wired; the rest are placeholders.
        struct ToolBtn
        {
            const char* Icon;
            const char* Name;
            bool        Impl;
            MS::Tool    Tool;
        };
        const ToolBtn tools[] = {
             { ICON_MDI_CUBE, "Box", false, MS::Tool::None },
             { ICON_MDI_SPHERE, "Sphere", false, MS::Tool::None },
             { ICON_MDI_CYLINDER, "Cylinder", false, MS::Tool::None },
             { ICON_MDI_CONE, "Cone", false, MS::Tool::None },
             { ICON_MDI_STAIRS, "Stairs", false, MS::Tool::None },
             { ICON_MDI_GRID, "CubeGrid", true, MS::Tool::CubeGrid },
        };

        ImGui::Columns( 2, nullptr, false );
        for ( const ToolBtn& t : tools )
        {
            const bool active = t.Impl && ms.ActiveTool == t.Tool;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, sel );
            if ( !t.Impl )
                ImGui::BeginDisabled();
            char label[64];
            std::snprintf( label, sizeof( label ), "%s  %s", t.Icon, t.Name );
            if ( ImGui::Button( label, ImVec2( -1.0f, 30.0f ) ) && t.Impl )
            {
                ms.ActiveTool = t.Tool;
                Core::ViewportMode::Set( Core::EditorMode::Modeling ); // selecting a tool enters Modeling mode
            }
            if ( !t.Impl )
                ImGui::EndDisabled();
            if ( active )
                ImGui::PopStyleColor();
            ImGui::NextColumn();
        }
        ImGui::Columns( 1 );
        ImGui::Separator();

        // --- Tool Properties (CubeGrid) ---
        if ( ms.ActiveTool == MS::Tool::CubeGrid )
        {
            ImGui::TextUnformatted( "CubeGrid" );
            ImGui::Spacing();
            if ( ImGui::CollapsingHeader( "Output Type", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                int               o      = static_cast<int>( ms.OutputType );
                const char* const outs[] = { "Static Mesh", "Dynamic Mesh" };
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::Combo( "##out", &o, outs, IM_ARRAYSIZE( outs ) ) )
                    ms.OutputType = static_cast<MS::Output>( o );
            }
            if ( ImGui::CollapsingHeader( "Grid", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                ImGui::Text( "Grid Step: %.0f", ms.CellSize );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "-##gs" ) )
                    ms.CellSize = std::max( ms.CellSize * 0.5f, 1.0f );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "+##gs" ) )
                    ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );
                ImGui::Text( "Cubes: %d", ms.Cubes );
                if ( ImGui::Button( "Clear" ) )
                    ms.ReqClear = true;
            }
            ImGui::Separator();
            ImGui::TextDisabled( "LMB drag: paint cells" );
            ImGui::TextDisabled( "Shift / RMB: erase" );
            ImGui::TextDisabled( "E / Q: extrude stroke out / in" );
            ImGui::TextDisabled( "Ctrl+E / Ctrl+Q: grid step" );
            ImGui::TextDisabled( "Accept / Cancel: bottom of the viewport" );
        }
        else
        {
            ImGui::TextDisabled( "Pick a tool above (CubeGrid) to begin." );
        }
        ImGui::EndChild();
    }
} // namespace Desert::Editor
