#pragma once

#include <algorithm>

namespace Desert::Editor::UI
{
    /// A window's rectangle in screen coordinates, as GLFW reports it: position of the top-left corner and
    /// the size of the client area.
    struct WindowRect
    {
        int X = 0;
        int Y = 0;
        int W = 0;
        int H = 0;
    };

    /// WHERE A WINDOW ENDS UP WHEN AN EDGE OR A CORNER IS DRAGGED.
    ///
    /// Split out of WindowChrome so the ARITHMETIC is testable without a window, a device or a mouse — the
    /// gesture that drives it cannot be synthesised on this machine (no assistive access), so the only part
    /// of a resize that can be asserted is this, and it is also the only part with anywhere to hide a bug.
    ///
    /// @p gripX / @p gripY are the grip's sign per axis: -1 drags the left/top edge, +1 the right/bottom
    /// edge, 0 leaves that axis untouched. So (-1,-1) is the top-left corner and (0,+1) the bottom edge.
    ///
    /// THE RELATION THIS FUNCTION EXISTS TO KEEP: dragging one edge must never move the opposite one. That
    /// is trivially true until the window reaches its minimum size, at which point clamping the SIZE alone
    /// keeps letting the position slide — the window shrinks to its minimum and then walks off across the
    /// desktop under a cursor that is only trying to make it smaller. Both are clamped together here, and
    /// Desert/Tests/Editor/WindowResize asserts the relation rather than the two values.
    inline WindowRect ResizeFromGrip( const WindowRect& start, int gripX, int gripY, int deltaX, int deltaY,
                                      int minW, int minH )
    {
        WindowRect out = start;

        if ( gripX < 0 )
        {
            // Clamped as a DISPLACEMENT, not as a width: this is the same number moving the left edge and
            // shrinking the window, so one clamp has to serve both or they disagree at the limit.
            const int dx = std::min( deltaX, start.W - minW );
            out.X        = start.X + dx;
            out.W        = start.W - dx;
        }
        else if ( gripX > 0 )
        {
            out.W = std::max( minW, start.W + deltaX );
        }

        if ( gripY < 0 )
        {
            const int dy = std::min( deltaY, start.H - minH );
            out.Y        = start.Y + dy;
            out.H        = start.H - dy;
        }
        else if ( gripY > 0 )
        {
            out.H = std::max( minH, start.H + deltaY );
        }

        return out;
    }
} // namespace Desert::Editor::UI
