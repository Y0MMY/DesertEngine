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

        // A section header for the Details panel: UE's flat grey bar with a disclosure triangle, full
        // width, the same height as a property row. Pass a label optionally prefixed with an MDI icon.
        // Returns whether it is expanded — wrap the body in
        // `if (SectionHeader(...)) { ImGui::Indent(); ... ImGui::Unindent(); }`.
        //
        // `detail` is drawn right-aligned INSIDE the bar in the muted text colour, the way UE puts
        // "Triangles: 1,458  Vertices: 1,186" on its LOD header — a collapsed section still states its
        // headline fact. It is painted straight into the bar, so it adds no item and never steals the
        // header's click; it is dropped when the panel is too narrow to hold it beside the label.
        static bool SectionHeader( const char* label, bool defaultOpen = true, const char* detail = nullptr );

        // A full-width accent ("primary") button — for the one obvious action of a section (Convert, Create…).
        static bool AccentButton( const char* label, float height = 0.0f );

        // An asset slot drawn the way UE draws one: a SUNK, full-width field carrying the asset's name
        // with a chevron at its right edge — not a raised push button. The distinction is the whole
        // reason UE's Details reads as a form: a raised button says "this does something", a sunk field
        // says "this holds a value you can change". Returns true when clicked (open the picker).
        static bool AssetSlot( const char* id, const char* text, bool empty = false );

        // --- Property rows -------------------------------------------------------------------------
        // THE row look of the editor, in one place — modelled on UE's Details grid: a uniform row
        // background, a 1px dark rule between rows, a vertical rule splitting label from value, a hover
        // band, and a label column whose width follows the panel. Hand-written widgets call
        // BeginPropertyRow/EndPropertyRow; the reflected grid draws its own controls but shares the
        // furniture through PropertyRowBackground + PropertyColumnRule, so a mesh widget and an
        // auto-built component cannot drift into looking like two different editors.
        //
        //   ResetPropertyRows();                       // once per component
        //   BeginPropertyRow( "Mesh Type" );           // value widget goes here, already width-clamped
        //   EndPropertyRow();
        static void ResetPropertyRows();
        // Draws the background for the row that is ABOUT to be submitted and returns whether the pointer
        // is over it (a fill drawn after the row would cover the widgets). `height` overrides the assumed
        // row height for rows taller than one control (a material slot with its preview) — without it the
        // rule and the hover band would cut straight through the row's content.
        static bool PropertyRowBackground( float height = 0.0f );
        // The vertical rule between the label and value columns, for the row currently being submitted.
        // Call it while the columns are still open, just before closing them.
        static void PropertyColumnRule();
        // Width of the label column — shared so every row in the panel breaks at the same x.
        static float PropertyLabelWidth();
        // Opens a two-column row and writes the label; the caller then submits ONE value widget.
        static void BeginPropertyRow( const char* label, const char* tooltip = nullptr, float height = 0.0f );
        static void EndPropertyRow();
    };
} // namespace Desert::Editor::Utils