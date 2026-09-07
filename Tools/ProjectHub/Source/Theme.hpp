#pragma once

// The launcher's palette, its fonts, and the eight drawing primitives every screen is built out of.
//
// PALETTE: structure and metric come from the measured UE5 values (panel #242424, section #2F2F2F,
// sunken field #0F0F0F, text #C8C8C8) so the launcher reads as the same product as the editor; the
// COLOUR is Desert's own — near-black #0E0E12 and sand #E8872B — because the launcher is the
// engine's brand surface and the editor's blue accent is not it. Selection is a sand tint rather
// than the editor's blue-grey: the launcher has no viewport that a coloured selection could
// misreport, so it can afford to say "selected" in the brand colour.
//
// Sand is spent on exactly three things — the active rail item, the one primary button on a screen,
// and selection. Everything else is grey. A palette where the accent is everywhere is a palette
// that cannot point at anything.

#include <imgui.h>

#include <string>

namespace Hub::Theme
{
    // clang-format off
    constexpr ImU32 RGB( int r, int g, int b ) { return IM_COL32( r, g, b, 255 ); }

    inline constexpr ImU32 kInk        = RGB( 0x0E, 0x0E, 0x12 ); // brand near-black: sidebar + chrome
    inline constexpr ImU32 kCanvas     = RGB( 0x17, 0x17, 0x1B ); // the field the tiles sit on
    inline constexpr ImU32 kPanel      = RGB( 0x24, 0x24, 0x24 ); // measured UE5 — the tile
    inline constexpr ImU32 kPanelAlt   = RGB( 0x2F, 0x2F, 0x2F ); // measured UE5 — hover / section bar
    inline constexpr ImU32 kPanelDim   = RGB( 0x1E, 0x1E, 0x20 ); // an entry that cannot be opened
    inline constexpr ImU32 kInput      = RGB( 0x0F, 0x0F, 0x0F ); // measured UE5 — sunken field
    inline constexpr ImU32 kBorder     = RGB( 0x33, 0x33, 0x36 );
    inline constexpr ImU32 kBorderSoft = RGB( 0x26, 0x26, 0x2A );
    inline constexpr ImU32 kText       = RGB( 0xC8, 0xC8, 0xC8 ); // measured UE5
    inline constexpr ImU32 kTextDim    = RGB( 0x8C, 0x8C, 0x92 );
    inline constexpr ImU32 kTextFaint  = RGB( 0x64, 0x64, 0x6A );
    inline constexpr ImU32 kAccent     = RGB( 0xE8, 0x87, 0x2B ); // brand sand
    inline constexpr ImU32 kAccentHi   = RGB( 0xF5, 0x9C, 0x4A );
    inline constexpr ImU32 kOnAccent   = RGB( 0x16, 0x10, 0x06 ); // text drawn on the sand
    inline constexpr ImU32 kSelectFill = RGB( 0x3A, 0x2B, 0x18 );
    inline constexpr ImU32 kError      = RGB( 0xFF, 0x73, 0x66 );
    inline constexpr ImU32 kWarning    = RGB( 0xF2, 0xBF, 0x59 );
    inline constexpr ImU32 kInfo       = RGB( 0x6E, 0xA8, 0xE8 );
    // clang-format on

    inline ImVec4 V4( ImU32 c )
    {
        return ImGui::ColorConvertU32ToFloat4( c );
    }
    inline ImU32 Fade( ImU32 c, float alpha )
    {
        ImVec4 v = V4( c );
        v.w *= alpha;
        return ImGui::GetColorU32( v );
    }
    ImU32 Mix( ImU32 a, ImU32 b, float t );

    // Six sizes, and no more: a launcher with eight text sizes is a launcher whose hierarchy is a
    // matter of opinion. BigIcon is the placeholder glyph on a tile with no picture.
    extern ImFont* Regular; // 15
    extern ImFont* Bold;    // 15
    extern ImFont* Small;   // 12.5
    extern ImFont* Tiny;    // 11
    extern ImFont* Title;   // 19 bold
    extern ImFont* H1;      // 26 bold
    extern ImFont* BigIcon; // 40

    // Loads the fonts from `<engineRoot>/Editor/Resources/Fonts`, falling back to ImGui's built-in
    // when they are not there — a launcher that cannot find an engine still has to draw.
    void LoadFonts( const std::string& engineRoot );
    void ApplyStyle();

    // ── geometry ─────────────────────────────────────────────────────────────────────────────────

    struct Rect
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;

        [[nodiscard]] float W() const
        {
            return x1 - x0;
        }
        [[nodiscard]] float H() const
        {
            return y1 - y0;
        }
        [[nodiscard]] ImVec2 Min() const
        {
            return ImVec2( x0, y0 );
        }
        [[nodiscard]] ImVec2 Max() const
        {
            return ImVec2( x1, y1 );
        }
        [[nodiscard]] bool Contains( ImVec2 p ) const
        {
            return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1;
        }
    };

    // ── the drawing vocabulary ───────────────────────────────────────────────────────────────────

    void  Box( Rect r, ImU32 fill, ImU32 border = 0, float rounding = 0.0f );
    void  TextAt( ImVec2 p, ImU32 col, const char* text, ImFont* font = nullptr );
    void  Wrapped( ImVec2 p, ImU32 col, const char* text, float width, ImFont* font = nullptr );
    float TextW( const char* text, ImFont* font );
    // Trims from the END — a name identifies itself by its beginning.
    void Elide( ImVec2 p, ImU32 col, const char* text, float maxWidth, ImFont* font = nullptr );
    // Trims from the FRONT — the identifying end of a path is its tail.
    void ElideFront( ImVec2 p, ImU32 col, const char* text, float maxWidth, ImFont* font = nullptr );
} // namespace Hub::Theme
