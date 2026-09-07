#include "ViewportModes.hpp"

namespace Desert::Editor::Core
{
    Graphic::DebugViewState ApplyViewportModes( const Graphic::DebugViewState& user, const ViewportModes& modes )
    {
        Graphic::DebugViewState effective = user;

        // 2D UI mode hides the infinite ground grid. The grid is a 3D orientation aid — a horizon line and
        // a floor to judge height against — and a screen-space canvas has neither, so it reads as a
        // hatch pattern behind the artwork. Unity's 2D scene view does the same thing.
        //
        // It suppresses, it never enables: a mode may take an overlay away from the picture the user asked
        // for, never add one they did not. `false` is therefore assigned rather than the flag being
        // replaced, so a user who has the grid off keeps it off in every mode.
        if ( modes.UI2D )
            effective.ShowGrid = false;

        return effective;
    }
} // namespace Desert::Editor::Core
