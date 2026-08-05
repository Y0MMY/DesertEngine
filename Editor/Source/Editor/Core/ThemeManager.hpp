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
    };

} // namespace Desert::Editor