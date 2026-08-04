#include "ModelingPanel.hpp"

#include <Editor/Core/Selection/ModelingState.hpp>
#include <Editor/Core/Selection/ViewportMode.hpp>

#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Common/Core/Units.hpp>
#include <ImGui/imgui.h>

#include <algorithm>

namespace Desert::Editor
{
    ModelingPanel::ModelingPanel( const std::shared_ptr<Desert::Core::Scene>& scene )
         : IPanel( "Modeling" ), m_Scene( scene )
    {
    }

    void ModelingPanel::OnPreUpdate()
    {
        // The Modeling palette only exists while the viewport is in Modeling mode (enter it via the mode
        // dropdown), mirroring UE5 — otherwise the panel is hidden entirely.
        GetVisibility() = Core::ViewportMode::Get() == Core::EditorMode::Modeling;
    }

    void ModelingPanel::OnUIRender()
    {
        using MS  = Core::ModelingState;
        auto& ms  = MS::Get();
        auto  sel = ImVec4( 0.20f, 0.55f, 0.95f, 1.0f ); // active highlight (matches the viewport toolbar)

        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.0f, 6.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 8.0f, 6.0f ) );

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

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 6.0f, 8.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 6.0f, 8.0f ) );
        ImGui::BeginChild( "##modeling_cats", ImVec2( 80.0f, 0.0f ), true );
        for ( int i = 0; i < static_cast<int>( IM_ARRAYSIZE( cats ) ); ++i )
        {
            const bool active = i == m_Category;
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, sel );
            char label[64];
            std::snprintf( label, sizeof( label ), "%s\n%s", cats[i].Icon, cats[i].Name );
            if ( ImGui::Button( label, ImVec2( -1.0f, 46.0f ) ) )
                m_Category = i;
            if ( active )
                ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar( 2 );

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
            ImGui::PopStyleVar( 2 );
            return;
        }

        if ( m_Category != 0 )
        {
            ImGui::TextDisabled( "This category is not implemented yet." );
            ImGui::EndChild();
            ImGui::PopStyleVar( 2 );
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
            // --- Asset Actions / Grid Reinitialization: the two top sections of UE's Cube Grid Tool ---
            if ( ImGui::CollapsingHeader( "Asset Actions", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                if ( ImGui::Button( "Accept and Start New", ImVec2( -1.0f, 0.0f ) ) )
                    ms.ReqAccept = true;
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Keep what you built as a mesh and start a fresh grid.\n"
                                       "The grid frame and Block Size carry over." );
            }
            if ( ImGui::CollapsingHeader( "Grid Reinitialization", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                if ( ImGui::Button( "Reset Grid from Actor", ImVec2( -1.0f, 0.0f ) ) )
                    ms.ReqResetFromActor = true;
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Put the grid origin on the SELECTED object's origin, so every block\n"
                                       "size stays flush with its corners instead of tiling from (0,0,0)." );
            }
            if ( ImGui::CollapsingHeader( "Options", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                // Moving the frame commits the current piece (cells are lattice indices) and re-tiles from
                // the new origin — already-built geometry keeps the frame it was made in and never moves.
                ImGui::SetNextItemWidth( -1.0f );
                ImGui::DragFloat3( "Grid Frame Origin", &ms.GridOrigin.x, 1.0f, 0.0f, 0.0f, "%.0f" );
                ImGui::Checkbox( "Show Gizmo", &ms.ShowGizmo );

                // Grid Power: block size = 1 m >> power (Power 2 = 25 cm), like UE's slider. Typing a free
                // Current Block Size below still wins — the power just snaps to the nearest step.
                int power = 0;
                for ( float sz = MS::BaseBlockSize; power < MS::MaxGridPower && sz > ms.CellSize + 0.01f; ++power )
                    sz *= 0.5f;
                ImGui::SetNextItemWidth( 120.0f );
                if ( ImGui::SliderInt( "Grid Power", &power, 0, MS::MaxGridPower ) )
                    ms.CellSize =
                         std::max( MS::MinCellSize, MS::BaseBlockSize / static_cast<float>( 1 << power ) );
            }
            if ( ImGui::CollapsingHeader( "Corner Mode", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                // Ramps / roofs / wedges: pick the selection's corner posts and raise or lower them.
                if ( ms.CornerMode )
                    ImGui::PushStyleColor( ImGuiCol_Button, sel );
                if ( ImGui::Button( ms.CornerMode ? "Corner Mode: ON  (Z)" : "Corner Mode: OFF  (Z)",
                                    ImVec2( -1.0f, 0.0f ) ) )
                    ms.ReqCornerMode = true;
                if ( ms.CornerMode )
                    ImGui::PopStyleColor();

                // Snap Size — how far one E/Q press moves a post, as a fraction of the block height.
                const char* const snaps[] = { "1/2 block", "1/4 block", "1/10 block" };
                const int         divs[]  = { 2, 4, 10 };
                int               cur     = 0;
                for ( int i = 0; i < 3; ++i )
                    if ( divs[i] == ms.CornerSnapDiv )
                        cur = i;
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::Combo( "Snap Size", &cur, snaps, 3 ) )
                    ms.CornerSnapDiv = divs[cur];

                ImGui::TextDisabled( "Select a rectangle, press Z, click the" );
                ImGui::TextDisabled( "corner posts (Shift adds), then E / Q." );
            }
            if ( ImGui::CollapsingHeader( "Block Selection", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                ImGui::Checkbox( "Hit Unrelated Geometry", &ms.HitUnrelated );
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "Target other objects in the scene too, so you can start a grid on\n"
                                       "top of an existing mesh (bounding-box level)." );
            }
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
                const float kMinBlock = MS::MinCellSize;
                // Block Size (grid step) in world units = CENTIMETRES, like UE: 100 is a one-metre block.
                // Already-drawn blocks never change; a coarser step just stamps bigger, a step finer than the
                // base subdivides the base. The value snaps to a base multiple after editing (the tool shows
                // the effective size).
                ImGui::SetNextItemWidth( 96.0f );
                if ( ImGui::DragFloat( "Current Block Size", &ms.CellSize, 1.0f, kMinBlock, 100000.0f,
                                       "%.0f cm" ) )
                    ms.CellSize = std::max( kMinBlock, ms.CellSize );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "/2##bs" ) )
                    ms.CellSize = std::max( ms.CellSize * 0.5f, kMinBlock );
                ImGui::SameLine();
                if ( ImGui::SmallButton( "x2##bs" ) )
                    ms.CellSize = std::min( ms.CellSize * 2.0f, 100000.0f );
                // Blocks Per Step: how many cells one Push/Pull moves (UE multiplier).
                ImGui::SetNextItemWidth( 120.0f );
                if ( ImGui::SliderInt( "Blocks / Step", &ms.BlocksPerStep, 1, 32 ) )
                    ms.BlocksPerStep = std::max( 1, ms.BlocksPerStep );
                ImGui::Text( "Cells: %d", ms.Cubes );
                if ( ImGui::Button( "Clear" ) )
                    ms.ReqClear = true;
            }
            ImGui::Separator();
            // Shortcut Info — same block UE shows at the bottom of the Cube Grid Tool panel.
            if ( ImGui::CollapsingHeader( "Shortcut Info", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                const struct
                {
                    const char* Action;
                    const char* Keys;
                } shortcuts[] = {
                     { "Select blocks", "LMB drag on the surface" },
                     { "Push / Pull", "E / Q" },
                     { "Corner Mode", "Z (then E / Q on posts)" },
                     { "Resize Grid", "Ctrl + E / Q" },
                     { "Shift work-plane", "Ctrl + Mouse Wheel" },
                     { "Snap grid to surface", "Ctrl + MMB" },
                     { "Clear selection", "Esc" },
                     { "Fly the camera", "RMB + WASD / Q / E" },
                };
                ImGui::Columns( 2, "##cg_keys", false );
                ImGui::SetColumnWidth( 0, 130.0f );
                for ( const auto& s : shortcuts )
                {
                    ImGui::TextDisabled( "%s", s.Action );
                    ImGui::NextColumn();
                    ImGui::TextUnformatted( s.Keys );
                    ImGui::NextColumn();
                }
                ImGui::Columns( 1 );
            }
            ImGui::TextDisabled( "Accept / Cancel: bottom of the viewport" );
        }
        else
        {
            ImGui::TextDisabled( "Pick a tool above (CubeGrid) to begin." );
        }
        ImGui::EndChild();
        ImGui::PopStyleVar( 2 );
    }
} // namespace Desert::Editor
