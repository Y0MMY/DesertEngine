#pragma once

#include <string>

struct ImColor;
struct ImRect;
struct ImVec2;
struct ImVec4;

namespace Desert::Editor::Utils
{
    class ImGuiUtilities
    {
    public:
        enum PropertyFlag
        {
            None           = 0,
            ColourProperty = 1u << 1,
            ReadOnly       = 1u << 2,
            DragValue      = 1u << 3,
            SliderValue    = 1u << 4,
        };

        static void PushID();
        static void PopID();

        static bool InputText( std::string& currentText, const char* ID );
        static void Tooltip( const char* text );
        static bool Property( const char* name, std::string& value, PropertyFlag flags = PropertyFlag::ReadOnly );

        static void DrawBorder( ImVec2 rectMin, ImVec2 rectMax, const ImVec4& borderColour, float thickness = 1.0f,
                                float offsetX = 0.0f, float offsetY = 0.0f );
        static void DrawBorder( ImRect rect, float thickness = 1.0f, float rounding = 0.0f, float offsetX = 0.0f,
                                float offsetY = 0.0f );
        static ImRect RectExpanded( const ImRect& rect, float x, float y );
        static void   DrawItemActivityOutline( float rounding, bool drawWhenInactive, ImColor colourWhenActive );

        static ImRect GetItemRect();
    };
} // namespace Desert::Editor::Utils