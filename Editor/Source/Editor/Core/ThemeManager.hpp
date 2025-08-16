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
    };

} // namespace Desert::Editor