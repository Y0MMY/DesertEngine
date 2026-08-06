#pragma once

struct ImVec4;

namespace Desert::Editor
{
    class ThemeManager
    {
    public:
        static void SetDarkTheme();
        static void SetBlackTheme();

        static ImVec4 GetSelectedColor();
        static ImVec4 GetIconColor();

        // The flat grey bar behind a section header (UE's category bar). Lives here rather than in the
        // widget that draws it, so a section in a hand-written widget and a category in the reflected
        // grid cannot drift into two different greys.
        static ImVec4 GetSectionHeaderColor();

        // --- Semantic colours ------------------------------------------------------------------------
        // Everything below was a literal repeated across panels, which is how the same idea ends up in
        // three different oranges. A colour that MEANS something belongs here, next to the palette it has
        // to live with; a colour that is just decoration on one widget does not.

        // World axes — X red, Y green, Z blue. The SAME colours in the viewport gizmo, the CubeGrid axis
        // handles and a vector field's edges, because they all point at the same three directions.
        // @p axis 0..2; anything else returns the neutral grey used for a fourth (W) component.
        static ImVec4 GetAxisColor( int axis );

        // The "you are acting on this right now" amber: an armed tool, a live grid plane, a mode button
        // that is on.
        static ImVec4 GetHighlightColor();

        // Something is wrong but usable (amber) / is broken (red) / checks out (green).
        static ImVec4 GetWarningColor();
        static ImVec4 GetErrorColor();
        static ImVec4 GetSuccessColor();
    };

} // namespace Desert::Editor