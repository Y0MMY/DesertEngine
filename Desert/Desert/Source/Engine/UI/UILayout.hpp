#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

// Godot-Control-style UI layout resolution — pure math, decoupled from ECS (takes raw anchor/offset/size
// values) so it is unit-testable and reusable by both the renderer and the editor preview.
namespace Desert::UI
{
    struct Rect
    {
        float X = 0.0f;
        float Y = 0.0f;
        float W = 0.0f;
        float H = 0.0f;
    };

    // Resolve an element's pixel rect from anchors (fraction 0..1 of the parent rect), pixel offsets from the
    // anchored edges (OffsetMin from the AnchorMin/left+top edge, OffsetMax from the AnchorMax/right+bottom
    // edge) and a custom minimum size, against the parent's pixel rect. AnchorMin==AnchorMax gives a
    // fixed-size element positioned by the offsets; spread anchors make it stretch with the parent.
    inline Rect ResolveRect( const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& offsetMin,
                             const glm::vec2& offsetMax, const glm::vec2& minSize, const Rect& parent )
    {
        const float left   = parent.X + anchorMin.x * parent.W + offsetMin.x;
        const float top    = parent.Y + anchorMin.y * parent.H + offsetMin.y;
        const float right  = parent.X + anchorMax.x * parent.W + offsetMax.x;
        const float bottom = parent.Y + anchorMax.y * parent.H + offsetMax.y;

        Rect r;
        r.X = left;
        r.Y = top;
        r.W = std::max( right - left, std::max( 0.0f, minSize.x ) );
        r.H = std::max( bottom - top, std::max( 0.0f, minSize.y ) );
        return r;
    }

    // Canvas root rect: the design (reference) resolution scaled uniformly to FIT the viewport, centred
    // (letterboxed) — so a layout authored at the reference size keeps its proportions on any window size.
    inline Rect CanvasRect( float refW, float refH, float viewW, float viewH )
    {
        if ( refW <= 0.0f || refH <= 0.0f )
            return { 0.0f, 0.0f, viewW, viewH };
        const float scale = std::min( viewW / refW, viewH / refH );
        const float w     = refW * scale;
        const float h     = refH * scale;
        return { ( viewW - w ) * 0.5f, ( viewH - h ) * 0.5f, w, h };
    }

    // ---------------------------------------------------------------------------------------------------
    // Auto-layout groups (VBox / HBox / Grid). A container with a layout group POSITIONS + SIZES its direct
    // children automatically, overriding their own anchors — the Unity/Godot "layout group" model. Pure math
    // (takes the container rect + each child's preferred size) so it is unit-testable and shared by the
    // renderer + editor. `Spacing`/`Padding`/`CellSize` are in the SAME pixel space as the container rect
    // (the caller pre-multiplies by the canvas scale).

    enum class LayoutGroupType
    {
        Horizontal, // HBox: children left -> right
        Vertical,   // VBox: children top -> bottom
        Grid        // fixed cells, wrapping into rows
    };

    struct LayoutGroupParams
    {
        LayoutGroupType Type     = LayoutGroupType::Vertical;
        float           PaddingL = 0.0f, PaddingT = 0.0f, PaddingR = 0.0f, PaddingB = 0.0f;
        float           Spacing = 0.0f; // gap between children (both axes for Grid)
        bool      StretchCross  = true; // stretch children across the minor axis (else keep preferred + centre)
        glm::vec2 CellSize      = { 100.0f, 100.0f }; // Grid only
        int       Columns       = 0;                  // Grid: fixed column count, 0 = auto-fit by width
    };

    // Lay `childSizes` (each child's preferred px size) out inside `container`, returning one rect per child.
    inline std::vector<Rect> SolveLayoutGroup( const Rect& container, const LayoutGroupParams& p,
                                               const std::vector<glm::vec2>& childSizes )
    {
        std::vector<Rect> out;
        out.reserve( childSizes.size() );

        const float x0     = container.X + p.PaddingL;
        const float y0     = container.Y + p.PaddingT;
        const float innerW = std::max( 0.0f, container.W - p.PaddingL - p.PaddingR );
        const float innerH = std::max( 0.0f, container.H - p.PaddingT - p.PaddingB );

        if ( p.Type == LayoutGroupType::Horizontal )
        {
            float x = x0;
            for ( const glm::vec2& s : childSizes )
            {
                const float h = p.StretchCross ? innerH : s.y;
                const float y = p.StretchCross ? y0 : y0 + ( innerH - h ) * 0.5f;
                out.push_back( { x, y, s.x, h } );
                x += s.x + p.Spacing;
            }
        }
        else if ( p.Type == LayoutGroupType::Vertical )
        {
            float y = y0;
            for ( const glm::vec2& s : childSizes )
            {
                const float w = p.StretchCross ? innerW : s.x;
                const float x = p.StretchCross ? x0 : x0 + ( innerW - w ) * 0.5f;
                out.push_back( { x, y, w, s.y } );
                y += s.y + p.Spacing;
            }
        }
        else // Grid
        {
            const float cw   = std::max( 1.0f, p.CellSize.x );
            const float ch   = std::max( 1.0f, p.CellSize.y );
            int         cols = p.Columns > 0
                                    ? p.Columns
                                    : std::max( 1, static_cast<int>( ( innerW + p.Spacing ) / ( cw + p.Spacing ) ) );
            for ( std::size_t i = 0; i < childSizes.size(); ++i )
            {
                const int col = static_cast<int>( i ) % cols;
                const int row = static_cast<int>( i ) / cols;
                out.push_back( { x0 + col * ( cw + p.Spacing ), y0 + row * ( ch + p.Spacing ), cw, ch } );
            }
        }
        return out;
    }
} // namespace Desert::UI
