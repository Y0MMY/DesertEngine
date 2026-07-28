#include "SequencerPanel.hpp"

#include <Editor/Core/IconsMaterialDesignIcons.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    SequencerPanel::SequencerPanel() : IPanel( "Sequencer", /*showPanel=*/false )
    {
        m_Tracks = {
            { "Transform / Position", IM_COL32( 110, 160, 220, 255 ), { 0.0f, 1.5f, 4.0f, 7.5f } },
            { "Transform / Rotation", IM_COL32( 110, 200, 160, 255 ), { 0.0f, 3.0f, 6.0f } },
            { "Material / Emission", IM_COL32( 230, 180, 90, 255 ), { 2.0f, 2.5f, 3.0f, 8.0f } },
            { "Events", IM_COL32( 220, 120, 120, 255 ), { 5.0f } },
        };
    }

    void SequencerPanel::OnUIRender()
    {
        // --- Transport (visual only) ---
        ImGui::BeginDisabled();
        ImGui::Button( ICON_MDI_SKIP_BACKWARD );
        ImGui::SameLine();
        ImGui::Button( ICON_MDI_PLAY );
        ImGui::SameLine();
        ImGui::Button( ICON_MDI_STOP );
        ImGui::SameLine();
        ImGui::Button( ICON_MDI_SKIP_FORWARD );
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Text( "  %05.2f s", m_Playhead );
        ImGui::SameLine( 0.0f, 24.0f );
        ImGui::SetNextItemWidth( 140.0f );
        ImGui::SliderFloat( "Zoom", &m_PxPerSec, 30.0f, 240.0f, "%.0f px/s" );
        ImGui::SameLine();
        ImGui::TextDisabled( "Preview — animation/cutscene tooling will build on this." );

        const float labelW = 190.0f;
        const float rowH   = 26.0f;
        const float rulerH = 22.0f;

        ImGui::BeginChild( "##seqBody", ImVec2( 0, 0 ), true, ImGuiWindowFlags_HorizontalScrollbar );

        ImDrawList*  dl     = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float  width  = labelW + m_Duration * m_PxPerSec + 60.0f;
        const float  height = rulerH + rowH * m_Tracks.size();

        auto timeToX = [&]( float t ) { return origin.x + labelW + t * m_PxPerSec; };

        // --- Ruler ---
        dl->AddRectFilled( ImVec2( origin.x, origin.y ), ImVec2( origin.x + width, origin.y + rulerH ),
                           IM_COL32( 30, 30, 34, 255 ) );
        for ( int s = 0; s <= (int)m_Duration; ++s )
        {
            const float x = timeToX( (float)s );
            dl->AddLine( ImVec2( x, origin.y ), ImVec2( x, origin.y + height ), IM_COL32( 255, 255, 255, 26 ) );
            char buf[16];
            std::snprintf( buf, sizeof( buf ), "%ds", s );
            dl->AddText( ImVec2( x + 3.0f, origin.y + 3.0f ), IM_COL32( 200, 200, 200, 160 ), buf );
        }

        // --- Tracks ---
        for ( size_t i = 0; i < m_Tracks.size(); ++i )
        {
            const float y0 = origin.y + rulerH + rowH * i;
            dl->AddRectFilled( ImVec2( origin.x, y0 ), ImVec2( origin.x + width, y0 + rowH ),
                               ( i & 1 ) ? IM_COL32( 255, 255, 255, 6 ) : IM_COL32( 0, 0, 0, 0 ) );
            dl->AddRectFilled( ImVec2( origin.x, y0 ), ImVec2( origin.x + labelW, y0 + rowH ),
                               IM_COL32( 38, 38, 44, 255 ) );
            dl->AddText( ImVec2( origin.x + 8.0f, y0 + 5.0f ), IM_COL32( 220, 220, 220, 255 ),
                         m_Tracks[i].Name.c_str() );

            for ( float key : m_Tracks[i].Keys )
            {
                const float  x = timeToX( key );
                const float  y = y0 + rowH * 0.5f;
                const float  r = 5.0f;
                const ImVec2 pts[4] = { { x, y - r }, { x + r, y }, { x, y + r }, { x - r, y } };
                dl->AddConvexPolyFilled( pts, 4, m_Tracks[i].Color );
                dl->AddPolyline( pts, 4, IM_COL32( 0, 0, 0, 200 ), ImDrawFlags_Closed, 1.0f );
            }
        }

        // --- Playhead (draggable) ---
        {
            const float x = timeToX( m_Playhead );
            dl->AddLine( ImVec2( x, origin.y ), ImVec2( x, origin.y + height ),
                         IM_COL32( 255, 90, 90, 255 ), 2.0f );
            const ImVec2 grip0( x - 6.0f, origin.y ), grip1( x + 6.0f, origin.y + rulerH );
            dl->AddRectFilled( grip0, ImVec2( x + 6.0f, origin.y + rulerH ), IM_COL32( 255, 90, 90, 120 ) );

            const ImVec2 mouse = ImGui::GetMousePos();
            const bool   overGrip =
                 mouse.x >= grip0.x && mouse.x <= grip1.x && mouse.y >= grip0.y && mouse.y <= grip1.y;
            if ( overGrip && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                m_DraggingPlayhead = true;
            if ( m_DraggingPlayhead )
            {
                m_Playhead = std::clamp( ( mouse.x - origin.x - labelW ) / m_PxPerSec, 0.0f, m_Duration );
                if ( ImGui::IsMouseReleased( ImGuiMouseButton_Left ) )
                    m_DraggingPlayhead = false;
            }
        }

        // Reserve the drawn area so the child scrolls correctly.
        ImGui::Dummy( ImVec2( width, height ) );
        ImGui::EndChild();
    }
} // namespace Desert::Editor
