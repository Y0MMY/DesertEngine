#define IMGUI_DEFINE_MATH_OPERATORS

#include "ThemeManager.hpp"

#include <ImGui/imgui.h>
#include "imgui_internal.h"

namespace Desert::Editor
{
    static ImVec4 s_SelectedColor( 0.3f, 0.6f, 0.9f, 1.0f );
    static ImVec4 s_IconColor( 0.78f, 0.78f, 0.78f, 1.0f );

    // Shorthand: integer RGB → normalized ImVec4
    static constexpr ImVec4 C( int r, int g, int b, float a = 1.0f )
    {
        return ImVec4( r / 255.0f, g / 255.0f, b / 255.0f, a );
    }

    void ThemeManager::SetDarkTheme()
    {
        auto&    style  = ImGui::GetStyle();
        ImVec4*  colors = style.Colors;

        // ── Palette ─────────────────────────────────────────────────────────
        const ImVec4 Bg        = C(  28,  28,  28 ); // #1C1C1C  main window / child bg
        const ImVec4 BgInput   = C(  56,  56,  56 ); // #383838  input / frame bg
        const ImVec4 BgPopup   = C(  30,  30,  30 ); // #1E1E1E  popup bg
        const ImVec4 TabActive = C(  45,  45,  45 ); // #2D2D2D  focused tab / active panel
        const ImVec4 TabInact  = C(  20,  20,  20 ); // #141414  unfocused tab
        const ImVec4 TitleBar  = C(  20,  20,  20 ); // #141414  title-bar / menu-bar
        const ImVec4 Border    = C(  55,  55,  55 ); // #373737  borders / separators
        const ImVec4 Btn       = C(  50,  50,  50 ); // #323232  button normal
        const ImVec4 BtnHov    = C(  65,  65,  65 ); // #414141  button hovered
        const ImVec4 BtnAct    = C(  38,  38,  38 ); // #262626  button active (pressed)
        const ImVec4 Select    = C(  30,  92, 181 ); // #1E5CB5  selection / header selected
        const ImVec4 SelectHov = C(  40, 105, 198 ); // #2869C6  hovered selection
        const ImVec4 SelectAct = C(  22,  72, 148 ); // #164894  active selection
        const ImVec4 Accent    = C(  77, 155, 230 ); // #4D9BE6  checkmark / slider / accent

        s_SelectedColor = Accent;
        s_IconColor     = C( 180, 180, 180 );

        // ── Text ─────────────────────────────────────────────────────────────
        colors[ImGuiCol_Text]         = C( 212, 212, 212 ); // #D4D4D4
        colors[ImGuiCol_TextDisabled] = C( 108, 108, 108 ); // #6C6C6C

        // ── Windows ──────────────────────────────────────────────────────────
        colors[ImGuiCol_WindowBg]  = Bg;
        colors[ImGuiCol_ChildBg]   = Bg;
        colors[ImGuiCol_PopupBg]   = BgPopup;

        // ── Borders ──────────────────────────────────────────────────────────
        colors[ImGuiCol_Border]       = Border;
        colors[ImGuiCol_BorderShadow] = ImVec4( 0, 0, 0, 0 );

        // ── Frames (input fields, combos) ────────────────────────────────────
        colors[ImGuiCol_FrameBg]        = BgInput;
        colors[ImGuiCol_FrameBgHovered] = C(  70,  70,  70 );
        colors[ImGuiCol_FrameBgActive]  = C(  44,  44,  44 );

        // ── Title-bars ────────────────────────────────────────────────────────
        colors[ImGuiCol_TitleBg]          = TitleBar;
        colors[ImGuiCol_TitleBgActive]    = TitleBar;
        colors[ImGuiCol_TitleBgCollapsed] = TitleBar;
        colors[ImGuiCol_MenuBarBg]        = TitleBar;

        // ── Scrollbars ────────────────────────────────────────────────────────
        colors[ImGuiCol_ScrollbarBg]          = C( 20, 20, 20,  0 );
        colors[ImGuiCol_ScrollbarGrab]        = C( 75, 75, 75 );
        colors[ImGuiCol_ScrollbarGrabHovered] = C( 95, 95, 95 );
        colors[ImGuiCol_ScrollbarGrabActive]  = C( 115, 115, 115 );

        // ── Checkmarks / sliders ─────────────────────────────────────────────
        colors[ImGuiCol_CheckMark]        = Accent;
        colors[ImGuiCol_SliderGrab]       = Accent;
        colors[ImGuiCol_SliderGrabActive] = C( 110, 175, 240 );

        // ── Buttons ───────────────────────────────────────────────────────────
        colors[ImGuiCol_Button]        = Btn;
        colors[ImGuiCol_ButtonHovered] = BtnHov;
        colors[ImGuiCol_ButtonActive]  = BtnAct;

        // ── Headers (TreeNode selected, Selectable selected) ──────────────────
        colors[ImGuiCol_Header]        = Select;
        colors[ImGuiCol_HeaderHovered] = SelectHov;
        colors[ImGuiCol_HeaderActive]  = SelectAct;

        // ── Separators ────────────────────────────────────────────────────────
        colors[ImGuiCol_Separator]        = Border;
        colors[ImGuiCol_SeparatorHovered] = C( 85, 85, 85 );
        colors[ImGuiCol_SeparatorActive]  = C( 110, 110, 110 );

        // ── Resize grips ──────────────────────────────────────────────────────
        colors[ImGuiCol_ResizeGrip]        = C( 77, 155, 230,  25 );
        colors[ImGuiCol_ResizeGripHovered] = C( 77, 155, 230, 170 );
        colors[ImGuiCol_ResizeGripActive]  = C( 77, 155, 230, 242 );

        // ── Tabs ──────────────────────────────────────────────────────────────
        colors[ImGuiCol_Tab]                = TabInact;
        colors[ImGuiCol_TabHovered]         = TabActive;
        colors[ImGuiCol_TabActive]          = TabActive;
        colors[ImGuiCol_TabUnfocused]       = TabInact;
        colors[ImGuiCol_TabUnfocusedActive] = C( 37, 37, 37 );

        // ── Docking ───────────────────────────────────────────────────────────
        colors[ImGuiCol_DockingEmptyBg] = C( 18, 18, 18 );
        colors[ImGuiCol_DockingPreview] = C( 30, 92, 181, 178 );

        // ── Tables ────────────────────────────────────────────────────────────
        colors[ImGuiCol_TableHeaderBg]     = C( 35, 35, 35 );
        colors[ImGuiCol_TableBorderStrong] = Border;
        colors[ImGuiCol_TableBorderLight]  = C( 45, 45, 45 );
        colors[ImGuiCol_TableRowBg]        = ImVec4( 0, 0, 0, 0 );
        colors[ImGuiCol_TableRowBgAlt]     = C( 33, 33, 33 );

        // ── Misc ──────────────────────────────────────────────────────────────
        colors[ImGuiCol_PlotLines]             = C( 156, 156, 156 );
        colors[ImGuiCol_PlotLinesHovered]      = Accent;
        colors[ImGuiCol_PlotHistogram]         = C( 230, 179,   0 );
        colors[ImGuiCol_PlotHistogramHovered]  = C( 255, 153,   0 );
        colors[ImGuiCol_TextSelectedBg]        = C(  30,  92, 181,  89 );
        colors[ImGuiCol_DragDropTarget]        = Accent;
        colors[ImGuiCol_NavHighlight]          = Accent;
        colors[ImGuiCol_NavWindowingHighlight] = C( 255, 255, 255, 178 );
        colors[ImGuiCol_NavWindowingDimBg]     = C( 204, 204, 204,  51 );
        colors[ImGuiCol_ModalWindowDimBg]      = C(  20,  20,  20, 140 );

        // ── Style / sizes ─────────────────────────────────────────────────────
        style.WindowPadding     = ImVec2(  8.0f,  8.0f );
        style.FramePadding      = ImVec2(  5.0f,  3.0f );
        style.CellPadding       = ImVec2(  4.0f,  2.0f );
        style.ItemSpacing       = ImVec2(  8.0f,  4.0f );
        style.ItemInnerSpacing  = ImVec2(  4.0f,  4.0f );
        style.IndentSpacing     = 18.0f;
        style.ScrollbarSize     = 10.0f;
        style.GrabMinSize       =  8.0f;

        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 0.0f;
        style.TabBorderSize     = 0.0f;

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 3.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 3.0f;

        style.WindowTitleAlign    = ImVec2( 0.0f, 0.5f );
        style.ColorButtonPosition = ImGuiDir_Left;
    }

    void ThemeManager::SetBlackTheme()
    {
        auto&    style  = ImGui::GetStyle();
        ImVec4*  colors = style.Colors;

        ImGui::StyleColorsDark();

        colors[ImGuiCol_Text]                  = ImVec4( 1.00f, 1.00f, 1.00f, 1.00f );
        colors[ImGuiCol_TextDisabled]          = ImVec4( 0.50f, 0.50f, 0.50f, 1.00f );
        colors[ImGuiCol_WindowBg]              = ImVec4( 0.10f, 0.10f, 0.10f, 1.00f );
        colors[ImGuiCol_ChildBg]               = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
        colors[ImGuiCol_PopupBg]               = ImVec4( 0.19f, 0.19f, 0.19f, 0.92f );
        colors[ImGuiCol_Border]                = ImVec4( 0.19f, 0.19f, 0.19f, 0.29f );
        colors[ImGuiCol_BorderShadow]          = ImVec4( 0.00f, 0.00f, 0.00f, 0.24f );
        colors[ImGuiCol_FrameBg]               = ImVec4( 0.05f, 0.05f, 0.05f, 0.54f );
        colors[ImGuiCol_FrameBgHovered]        = ImVec4( 0.19f, 0.19f, 0.19f, 0.54f );
        colors[ImGuiCol_FrameBgActive]         = ImVec4( 0.20f, 0.22f, 0.23f, 1.00f );
        colors[ImGuiCol_TitleBg]               = ImVec4( 0.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_TitleBgActive]         = ImVec4( 0.06f, 0.06f, 0.06f, 1.00f );
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4( 0.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_MenuBarBg]             = ImVec4( 0.14f, 0.14f, 0.14f, 1.00f );
        colors[ImGuiCol_ScrollbarBg]           = ImVec4( 0.05f, 0.05f, 0.05f, 0.54f );
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4( 0.34f, 0.34f, 0.34f, 0.54f );
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4( 0.40f, 0.40f, 0.40f, 0.54f );
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4( 0.56f, 0.56f, 0.56f, 0.54f );
        colors[ImGuiCol_CheckMark]             = ImVec4( 0.33f, 0.67f, 0.86f, 1.00f );
        colors[ImGuiCol_SliderGrab]            = ImVec4( 0.34f, 0.34f, 0.34f, 0.54f );
        colors[ImGuiCol_SliderGrabActive]      = ImVec4( 0.56f, 0.56f, 0.56f, 0.54f );
        colors[ImGuiCol_Button]                = ImVec4( 0.05f, 0.05f, 0.05f, 0.54f );
        colors[ImGuiCol_ButtonHovered]         = ImVec4( 0.19f, 0.19f, 0.19f, 0.54f );
        colors[ImGuiCol_ButtonActive]          = ImVec4( 0.20f, 0.22f, 0.23f, 1.00f );
        colors[ImGuiCol_Header]                = ImVec4( 0.00f, 0.00f, 0.00f, 0.52f );
        colors[ImGuiCol_HeaderHovered]         = ImVec4( 0.00f, 0.00f, 0.00f, 0.36f );
        colors[ImGuiCol_HeaderActive]          = ImVec4( 0.20f, 0.22f, 0.23f, 0.33f );
        colors[ImGuiCol_Separator]             = ImVec4( 0.28f, 0.28f, 0.28f, 0.29f );
        colors[ImGuiCol_SeparatorHovered]      = ImVec4( 0.44f, 0.44f, 0.44f, 0.29f );
        colors[ImGuiCol_SeparatorActive]       = ImVec4( 0.40f, 0.44f, 0.47f, 1.00f );
        colors[ImGuiCol_ResizeGrip]            = ImVec4( 0.28f, 0.28f, 0.28f, 0.29f );
        colors[ImGuiCol_ResizeGripHovered]     = ImVec4( 0.44f, 0.44f, 0.44f, 0.29f );
        colors[ImGuiCol_ResizeGripActive]      = ImVec4( 0.40f, 0.44f, 0.47f, 1.00f );
        colors[ImGuiCol_Tab]                   = ImVec4( 0.00f, 0.00f, 0.00f, 0.52f );
        colors[ImGuiCol_TabHovered]            = ImVec4( 0.14f, 0.14f, 0.14f, 1.00f );
        colors[ImGuiCol_TabActive]             = ImVec4( 0.20f, 0.20f, 0.20f, 0.36f );
        colors[ImGuiCol_TabUnfocused]          = ImVec4( 0.00f, 0.00f, 0.00f, 0.52f );
        colors[ImGuiCol_TabUnfocusedActive]    = ImVec4( 0.14f, 0.14f, 0.14f, 1.00f );
        colors[ImGuiCol_DockingPreview]        = ImVec4( 0.33f, 0.67f, 0.86f, 1.00f );
        colors[ImGuiCol_DockingEmptyBg]        = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_PlotLines]             = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_PlotLinesHovered]      = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_PlotHistogram]         = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_PlotHistogramHovered]  = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_TableHeaderBg]         = ImVec4( 0.00f, 0.00f, 0.00f, 0.52f );
        colors[ImGuiCol_TableBorderStrong]     = ImVec4( 0.00f, 0.00f, 0.00f, 0.52f );
        colors[ImGuiCol_TableBorderLight]      = ImVec4( 0.28f, 0.28f, 0.28f, 0.29f );
        colors[ImGuiCol_TextSelectedBg]        = ImVec4( 0.20f, 0.22f, 0.23f, 1.00f );
        colors[ImGuiCol_DragDropTarget]        = ImVec4( 0.33f, 0.67f, 0.86f, 1.00f );
        colors[ImGuiCol_NavHighlight]          = ImVec4( 1.00f, 0.00f, 0.00f, 1.00f );
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 0.00f, 0.00f, 0.70f );
        colors[ImGuiCol_NavWindowingDimBg]     = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
        colors[ImGuiCol_ModalWindowDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );
    }

    ImVec4 ThemeManager::GetSelectedColor()
    {
        return s_SelectedColor;
    }

    ImVec4 ThemeManager::GetIconColor()
    {
        return s_IconColor;
    }

} // namespace Desert::Editor
