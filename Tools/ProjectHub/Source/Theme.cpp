#include "Theme.hpp"

#include <cfloat>
#include <filesystem>

namespace Hub::Theme
{
    namespace fs = std::filesystem;

    ImFont* Regular = nullptr;
    ImFont* Bold    = nullptr;
    ImFont* Small   = nullptr;
    ImFont* Tiny    = nullptr;
    ImFont* Title   = nullptr;
    ImFont* H1      = nullptr;
    ImFont* BigIcon = nullptr;

    ImU32 Mix( ImU32 a, ImU32 b, float t )
    {
        const ImVec4 x = V4( a );
        const ImVec4 y = V4( b );
        return ImGui::GetColorU32( ImVec4( x.x + ( y.x - x.x ) * t, x.y + ( y.y - x.y ) * t,
                                           x.z + ( y.z - x.z ) * t, x.w + ( y.w - x.w ) * t ) );
    }

    void LoadFonts( const std::string& engineRoot )
    {
        ImGuiIO& io = ImGui::GetIO();

        const fs::path fontDirectory =
             fs::path( engineRoot.empty() ? "." : engineRoot ) / "Editor" / "Resources" / "Fonts";
        const fs::path body  = fontDirectory / "Roboto-Regular.ttf";
        const fs::path bold  = fontDirectory / "Roboto-Bold.ttf";
        const fs::path icons = fontDirectory / "materialdesignicons-webfont.ttf";

        if ( !fs::exists( body ) || !fs::exists( bold ) )
        {
            // No engine, no fonts. One face at one size for every role: the screens still lay out,
            // and "the launcher could not find an engine" is said in the sidebar rather than being
            // guessed at from ugly text.
            Regular = Bold = Small = Tiny = Title = H1 = BigIcon = io.Fonts->AddFontDefault();
            return;
        }

        static const ImWchar kIconRange[] = { 0xF0000, 0xF2000, 0 };
        const auto           add = [&]( const fs::path& ttf, float size, bool withIcons = true ) -> ImFont*
        {
            ImFont* font = io.Fonts->AddFontFromFileTTF( ttf.string().c_str(), size );
            if ( withIcons && fs::exists( icons ) )
            {
                ImFontConfig cfg;
                cfg.MergeMode     = true;
                cfg.GlyphOffset.y = 1.0f;
                io.Fonts->AddFontFromFileTTF( icons.string().c_str(), size, &cfg, kIconRange );
            }
            return font;
        };

        Regular = add( body, 15.0f );
        Bold    = add( bold, 15.0f );
        Small   = add( body, 12.5f );
        Tiny    = add( body, 11.0f );
        Title   = add( bold, 19.0f );
        H1      = add( bold, 26.0f );
        BigIcon = add( body, 40.0f );
    }

    void ApplyStyle()
    {
        ImGuiStyle& style       = ImGui::GetStyle();
        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 4.0f;
        style.PopupRounding     = 6.0f;
        style.GrabRounding      = 4.0f;
        style.WindowBorderSize  = 0.0f;
        style.ChildBorderSize   = 0.0f;
        style.FrameBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FramePadding      = ImVec2( 10.0f, 7.0f );
        style.ItemSpacing       = ImVec2( 8.0f, 8.0f );
        style.WindowPadding     = ImVec2( 0.0f, 0.0f );
        style.ScrollbarSize     = 8.0f;
        style.ScrollbarRounding = 4.0f;

        ImVec4* c                  = style.Colors;
        c[ImGuiCol_WindowBg]       = V4( kCanvas );
        c[ImGuiCol_ChildBg]        = ImVec4( 0, 0, 0, 0 );
        c[ImGuiCol_PopupBg]        = V4( kPanel );
        c[ImGuiCol_Border]         = V4( kBorder );
        c[ImGuiCol_Text]           = V4( kText );
        c[ImGuiCol_TextDisabled]   = V4( kTextFaint );
        c[ImGuiCol_FrameBg]        = V4( kInput );
        c[ImGuiCol_FrameBgHovered] = V4( Mix( kInput, kPanelAlt, 0.5f ) );
        c[ImGuiCol_FrameBgActive]  = V4( Mix( kInput, kPanelAlt, 0.7f ) );
        c[ImGuiCol_Button]         = V4( kPanelAlt );
        c[ImGuiCol_ButtonHovered]  = V4( Mix( kPanelAlt, kText, 0.12f ) );
        c[ImGuiCol_ButtonActive]   = V4( Mix( kPanelAlt, kInk, 0.3f ) );
        c[ImGuiCol_Header]         = V4( kPanelAlt );
        c[ImGuiCol_HeaderHovered]  = V4( Mix( kPanelAlt, kText, 0.12f ) );
        c[ImGuiCol_HeaderActive]   = V4( Fade( kAccent, 0.25f ) );
        c[ImGuiCol_ScrollbarBg]    = ImVec4( 0, 0, 0, 0 );
        c[ImGuiCol_ScrollbarGrab]  = ImVec4( 1, 1, 1, 0.14f );
        c[ImGuiCol_CheckMark]      = V4( kAccent );
        c[ImGuiCol_SliderGrab]     = V4( kAccent );
        c[ImGuiCol_Separator]      = ImVec4( 1, 1, 1, 0.06f );
    }

    void Box( Rect r, ImU32 fill, ImU32 border, float rounding )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if ( fill )
            dl->AddRectFilled( r.Min(), r.Max(), fill, rounding );
        if ( border )
            dl->AddRect( r.Min(), r.Max(), border, rounding );
    }

    void TextAt( ImVec2 p, ImU32 col, const char* text, ImFont* font )
    {
        ImFont* f = font ? font : Regular;
        ImGui::GetWindowDrawList()->AddText( f, f->FontSize, p, col, text );
    }

    void Wrapped( ImVec2 p, ImU32 col, const char* text, float width, ImFont* font )
    {
        ImFont* f = font ? font : Regular;
        ImGui::GetWindowDrawList()->AddText( f, f->FontSize, p, col, text, nullptr, width );
    }

    float TextW( const char* text, ImFont* font )
    {
        ImFont* f = font ? font : Regular;
        return f->CalcTextSizeA( f->FontSize, FLT_MAX, 0.0f, text ).x;
    }

    void Elide( ImVec2 p, ImU32 col, const char* text, float maxWidth, ImFont* font )
    {
        ImFont* f = font ? font : Regular;
        if ( TextW( text, f ) <= maxWidth )
        {
            TextAt( p, col, text, f );
            return;
        }
        // Three ASCII dots, not U+2026: the body font is loaded over the default (Latin-1) range
        // with only the icon block merged on top, so the real ellipsis draws as a missing-glyph box.
        std::string cut( text );
        while ( !cut.empty() && TextW( ( cut + "..." ).c_str(), f ) > maxWidth )
            cut.pop_back();
        TextAt( p, col, ( cut + "..." ).c_str(), f );
    }

    void ElideFront( ImVec2 p, ImU32 col, const char* text, float maxWidth, ImFont* font )
    {
        ImFont* f = font ? font : Regular;
        if ( TextW( text, f ) <= maxWidth )
        {
            TextAt( p, col, text, f );
            return;
        }
        std::string whole( text );
        size_t      start = 0;
        while ( start < whole.size() && TextW( ( "..." + whole.substr( start ) ).c_str(), f ) > maxWidth )
            ++start;
        TextAt( p, col, ( "..." + whole.substr( start ) ).c_str(), f );
    }
} // namespace Hub::Theme
