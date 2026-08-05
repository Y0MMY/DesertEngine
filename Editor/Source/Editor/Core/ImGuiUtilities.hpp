#pragma once

#include <string>
#include <glm/glm.hpp>

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
            None          = 0,
            ColorProperty = 1u << 1,
            ReadOnly      = 1u << 2,
            DragValue     = 1u << 3,
            SliderValue   = 1u << 4,
        };

        static void PushID();
        static void PopID();

        static bool InputText( std::string& currentText, const char* ID );
        static void Tooltip( const char* text );
        static bool Property( const char* name, std::string& value, PropertyFlag flags = PropertyFlag::ReadOnly );
        static bool Property( const char* name, float& value, float min = -1.0f, float max = 1.0f,
                              float delta = 1.0f, PropertyFlag flags = PropertyFlag::None );
        static bool Property( const char* name, glm::vec3& value, float min = -1.0f, float max = 1.0f,
                              bool exposeW = false, PropertyFlag flags = PropertyFlag::None );
        static bool Property( const char* name, bool& value, PropertyFlag flags = PropertyFlag::None );

        static bool Property( const char* name, glm::vec3& value, bool exposeW, PropertyFlag flags );

        static bool Property( const char* name, uint32_t& value, PropertyFlag flags = PropertyFlag::None );

        static void DrawBorder( ImVec2 rectMin, ImVec2 rectMax, const ImVec4& borderColour, float thickness = 1.0f,
                                float offsetX = 0.0f, float offsetY = 0.0f );
        static void DrawBorder( ImRect rect, float thickness = 1.0f, float rounding = 0.0f, float offsetX = 0.0f,
                                float offsetY = 0.0f );
        static ImRect RectExpanded( const ImRect& rect, float x, float y );
        static void   DrawItemActivityOutline( float rounding, bool drawWhenInactive, ImColor colourWhenActive );

        static ImRect GetItemRect();

        // A polished sub-section header for the Details panel: an accent-tinted, rounded collapsing header
        // (open by default). Pass a label optionally prefixed with an MDI icon. Returns whether it is expanded
        // — wrap the body in `if (SectionHeader(...)) { ImGui::Indent(); ... ImGui::Unindent(); }`.
        static bool SectionHeader( const char* label, bool defaultOpen = true );

        // A full-width accent ("primary") button — for the one obvious action of a section (Convert, Create…).
        static bool AccentButton( const char* label, float height = 0.0f );

        // --- Property rows -------------------------------------------------------------------------
        // THE row look of the editor, in one place: alternating stripe, hover band, a label column whose
        // width follows the panel, and a value column that fills the rest. Hand-written widgets call
        // BeginPropertyRow/EndPropertyRow; the reflected grid draws its own controls but shares the
        // background through PropertyRowBackground, so a mesh widget and an auto-built component cannot
        // drift into looking like two different editors.
        //
        //   ResetPropertyRows();                       // once per component, so striping starts alike
        //   if ( BeginPropertyRow( "Mesh Type" ) ) ... // value widget goes here, already width-clamped
        //   EndPropertyRow();
        static void ResetPropertyRows();
        // Draws the background for the row that is ABOUT to be submitted and returns whether the pointer
        // is over it (a fill drawn after the row would cover the widgets). Advances the stripe.
        static bool PropertyRowBackground();
        // Opens a two-column row and writes the label; the caller then submits ONE value widget.
        static void BeginPropertyRow( const char* label, const char* tooltip = nullptr );
        static void EndPropertyRow();
    };
} // namespace Desert::Editor::Utils