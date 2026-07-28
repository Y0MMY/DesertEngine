#include "NodeGraphPanel.hpp"

#include <algorithm>
#include <cmath>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        constexpr float kHeaderH   = 24.0f;
        constexpr float kPinRowH   = 20.0f;
        constexpr float kPinRadius = 4.5f;
        constexpr float kNodeW     = 170.0f;

        constexpr ImU32 kColFloat = IM_COL32( 145, 210, 130, 255 );
        constexpr ImU32 kColVec   = IM_COL32( 240, 200, 90, 255 );
        constexpr ImU32 kColColor = IM_COL32( 235, 120, 120, 255 );
    } // namespace

    NodeGraphPanel::NodeGraphPanel() : IPanel( "Node Graph", /*showPanel=*/false )
    {
        // Demo graph: a tiny "surface shader" sketch, purely visual.
        m_Nodes.push_back( { "Texture Sample",
                             ImVec2( 40.0f, 60.0f ),
                             ImVec2(),
                             { { "UV", kColVec } },
                             { { "RGBA", kColColor }, { "R", kColFloat } },
                             IM_COL32( 70, 110, 160, 255 ) } );
        m_Nodes.push_back( { "Multiply",
                             ImVec2( 300.0f, 40.0f ),
                             ImVec2(),
                             { { "A", kColColor }, { "B", kColFloat } },
                             { { "Out", kColColor } },
                             IM_COL32( 90, 90, 120, 255 ) } );
        m_Nodes.push_back( { "Scalar (0.5)",
                             ImVec2( 60.0f, 240.0f ),
                             ImVec2(),
                             {},
                             { { "Value", kColFloat } },
                             IM_COL32( 80, 130, 90, 255 ) } );
        m_Nodes.push_back( { "Surface Output",
                             ImVec2( 540.0f, 90.0f ),
                             ImVec2(),
                             { { "Albedo", kColColor }, { "Roughness", kColFloat }, { "Emission", kColColor } },
                             {},
                             IM_COL32( 150, 90, 60, 255 ) } );

        m_Links.push_back( { 0, 0, 1, 0 } ); // Texture.RGBA -> Multiply.A
        m_Links.push_back( { 2, 0, 1, 1 } ); // Scalar -> Multiply.B
        m_Links.push_back( { 1, 0, 3, 0 } ); // Multiply.Out -> Output.Albedo
        m_Links.push_back( { 0, 1, 3, 1 } ); // Texture.R -> Output.Roughness
    }

    ImVec2 NodeGraphPanel::PinPos( const Node& node, int pin, bool output ) const
    {
        const float y = node.Pos.y + kHeaderH + kPinRowH * ( pin + 0.5f );
        return { output ? node.Pos.x + node.Size.x : node.Pos.x, y };
    }

    void NodeGraphPanel::OnUIRender()
    {
        ImGui::TextDisabled( "Preview canvas — the real shader-graph / visual-scripting tools will build "
                             "on this. Drag nodes, pan (middle mouse), zoom (wheel)." );

        ImGui::BeginChild( "##nodeCanvas", ImVec2( 0, 0 ), true,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                ImGuiWindowFlags_NoMove );

        ImDrawList*  dl     = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        // Clamp to >= 1px: a collapsed/undocked-tiny panel reports 0 avail, and InvisibleButton
        // asserts on zero-sized rects.
        const ImVec2 availRaw = ImGui::GetContentRegionAvail();
        const ImVec2 avail    = ImVec2( std::max( availRaw.x, 1.0f ), std::max( availRaw.y, 1.0f ) );

        // canvas -> screen
        auto toScreen = [&]( const ImVec2& p )
        { return ImVec2( origin.x + ( p.x + m_Scroll.x ) * m_Zoom, origin.y + ( p.y + m_Scroll.y ) * m_Zoom ); };

        // --- Background grid ---
        const float grid = 32.0f * m_Zoom;
        const ImU32 gridCol = IM_COL32( 200, 200, 200, 18 );
        for ( float x = std::fmod( m_Scroll.x * m_Zoom, grid ); x < avail.x; x += grid )
            dl->AddLine( ImVec2( origin.x + x, origin.y ), ImVec2( origin.x + x, origin.y + avail.y ), gridCol );
        for ( float y = std::fmod( m_Scroll.y * m_Zoom, grid ); y < avail.y; y += grid )
            dl->AddLine( ImVec2( origin.x, origin.y + y ), ImVec2( origin.x + avail.x, origin.y + y ), gridCol );

        // Interactions on the empty canvas: middle-mouse pan + wheel zoom.
        ImGui::InvisibleButton( "##canvasInput", avail,
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle );
        const bool canvasHovered = ImGui::IsItemHovered();
        if ( canvasHovered && ImGui::IsMouseDragging( ImGuiMouseButton_Middle ) )
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            m_Scroll.x += d.x / m_Zoom;
            m_Scroll.y += d.y / m_Zoom;
        }
        if ( canvasHovered && ImGui::GetIO().MouseWheel != 0.0f )
            m_Zoom = std::clamp( m_Zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.4f, 2.0f );

        // --- Links (behind nodes) ---
        for ( const auto& link : m_Links )
        {
            if ( link.FromNode >= (int)m_Nodes.size() || link.ToNode >= (int)m_Nodes.size() )
                continue;
            const ImVec2 a = toScreen( PinPos( m_Nodes[link.FromNode], link.FromPin, true ) );
            const ImVec2 b = toScreen( PinPos( m_Nodes[link.ToNode], link.ToPin, false ) );
            const float  t = std::max( 30.0f, std::fabs( b.x - a.x ) * 0.5f ) * m_Zoom;
            dl->AddBezierCubic( a, ImVec2( a.x + t, a.y ), ImVec2( b.x - t, b.y ), b,
                                IM_COL32( 200, 200, 200, 160 ), 2.0f * m_Zoom );
        }

        // --- Nodes ---
        const ImVec2 mouse = ImGui::GetMousePos();
        for ( int i = 0; i < (int)m_Nodes.size(); ++i )
        {
            Node&       node = m_Nodes[i];
            const int   rows = std::max( node.Inputs.size(), node.Outputs.size() ) ? (int)std::max( node.Inputs.size(), node.Outputs.size() ) : 1;
            node.Size        = ImVec2( kNodeW, kHeaderH + kPinRowH * rows + 8.0f );

            const ImVec2 p0 = toScreen( node.Pos );
            const ImVec2 p1 = toScreen( ImVec2( node.Pos.x + node.Size.x, node.Pos.y + node.Size.y ) );

            const bool selected = ( m_SelectedNode == i );
            dl->AddRectFilled( p0, p1, IM_COL32( 42, 42, 48, 235 ), 6.0f * m_Zoom );
            dl->AddRectFilled( p0, ImVec2( p1.x, p0.y + kHeaderH * m_Zoom ), node.HeaderColor, 6.0f * m_Zoom,
                               ImDrawFlags_RoundCornersTop );
            dl->AddRect( p0, p1, selected ? IM_COL32( 255, 200, 90, 255 ) : IM_COL32( 15, 15, 15, 255 ),
                         6.0f * m_Zoom, 0, selected ? 2.0f : 1.0f );
            dl->AddText( ImVec2( p0.x + 8.0f * m_Zoom, p0.y + 5.0f * m_Zoom ), IM_COL32_WHITE,
                         node.Title.c_str() );

            // Pins + labels.
            for ( int pi = 0; pi < (int)node.Inputs.size(); ++pi )
            {
                const ImVec2 pp = toScreen( PinPos( node, pi, false ) );
                dl->AddCircleFilled( pp, kPinRadius * m_Zoom, node.Inputs[pi].Color );
                dl->AddText( ImVec2( pp.x + 8.0f * m_Zoom, pp.y - 7.0f * m_Zoom ),
                             IM_COL32( 210, 210, 210, 255 ), node.Inputs[pi].Name.c_str() );
            }
            for ( int po = 0; po < (int)node.Outputs.size(); ++po )
            {
                const ImVec2 pp  = toScreen( PinPos( node, po, true ) );
                const ImVec2 lsz = ImGui::CalcTextSize( node.Outputs[po].Name.c_str() );
                dl->AddCircleFilled( pp, kPinRadius * m_Zoom, node.Outputs[po].Color );
                dl->AddText( ImVec2( pp.x - lsz.x - 8.0f * m_Zoom, pp.y - 7.0f * m_Zoom ),
                             IM_COL32( 210, 210, 210, 255 ), node.Outputs[po].Name.c_str() );
            }

            // Header drag + selection.
            const bool overHeader = mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= p0.y &&
                                    mouse.y <= p0.y + kHeaderH * m_Zoom;
            if ( overHeader && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && canvasHovered )
            {
                m_DraggingNode = i;
                m_SelectedNode = i;
            }
        }

        if ( m_DraggingNode >= 0 )
        {
            if ( ImGui::IsMouseDragging( ImGuiMouseButton_Left ) )
            {
                const ImVec2 d = ImGui::GetIO().MouseDelta;
                m_Nodes[m_DraggingNode].Pos.x += d.x / m_Zoom;
                m_Nodes[m_DraggingNode].Pos.y += d.y / m_Zoom;
            }
            if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                m_DraggingNode = -1;
        }

        // Corner watermark.
        dl->AddText( ImVec2( origin.x + 10.0f, origin.y + avail.y - 24.0f ), IM_COL32( 255, 255, 255, 70 ),
                     "NODE GRAPH — PREVIEW" );

        ImGui::EndChild();
    }
} // namespace Desert::Editor
