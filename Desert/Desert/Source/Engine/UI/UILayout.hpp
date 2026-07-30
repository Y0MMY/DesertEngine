#pragma once

#include <glm/glm.hpp>

#include <algorithm>

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
} // namespace Desert::UI
