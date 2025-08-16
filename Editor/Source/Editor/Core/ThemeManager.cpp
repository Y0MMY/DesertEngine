#define IMGUI_DEFINE_MATH_OPERATORS

#include "ThemeManager.hpp"

#include <ImGui/imgui.h>
#include "imgui_internal.h"

namespace Desert::Editor
{
    static ImVec4 s_SelectedColor( 0.28f, 0.56f, 0.9f, 1.0f );
    static ImVec4 s_IconColor( 0.2f, 0.2f, 0.2f, 1.0f );

    void ThemeManager::SetDarkTheme()
    {
        static const float max = 255.0f;

        auto&   style  = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        ImGui::StyleColorsDark();
        ImVec4 Titlebar    = ImVec4( 40.0f / max, 42.0f / max, 54.0f / max, 1.0f );
        ImVec4 TabActive   = ImVec4( 52.0f / max, 54.0f / max, 64.0f / max, 1.0f );
        ImVec4 TabUnactive = ImVec4( 35.0f / max, 43.0f / max, 59.0f / max, 1.0f );

        s_SelectedColor               = ImVec4( 155.0f / 255.0f, 130.0f / 255.0f, 207.0f / 255.0f, 1.00f );
        colors[ImGuiCol_Text]         = ImVec4( 200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.00f );
        colors[ImGuiCol_TextDisabled] = ImVec4( 0.36f, 0.42f, 0.47f, 1.00f );

        s_IconColor               = colors[ImGuiCol_Text];
        colors[ImGuiCol_WindowBg] = TabActive;
        colors[ImGuiCol_ChildBg]  = TabActive;

        colors[ImGuiCol_PopupBg]        = ImVec4( 42.0f / 255.0f, 38.0f / 255.0f, 47.0f / 255.0f, 1.00f );
        colors[ImGuiCol_Border]         = ImVec4( 0.08f, 0.10f, 0.12f, 1.00f );
        colors[ImGuiCol_BorderShadow]   = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
        colors[ImGuiCol_FrameBg]        = ImVec4( 65.0f / 255.0f, 79.0f / 255.0f, 92.0f / 255.0f, 1.00f );
        colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.12f, 0.20f, 0.28f, 1.00f );
        colors[ImGuiCol_FrameBgActive]  = ImVec4( 0.09f, 0.12f, 0.14f, 1.00f );

        colors[ImGuiCol_TitleBg]          = Titlebar;
        colors[ImGuiCol_TitleBgActive]    = Titlebar;
        colors[ImGuiCol_TitleBgCollapsed] = Titlebar;
        colors[ImGuiCol_MenuBarBg]        = Titlebar;

        colors[ImGuiCol_ScrollbarBg]          = ImVec4( 0.02f, 0.02f, 0.02f, 0.39f );
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4( 0.6f, 0.6f, 0.6f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4( 0.7f, 0.7f, 0.7f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.8f, 0.8f, 0.8f, 1.00f );

        colors[ImGuiCol_CheckMark]        = ImVec4( 155.0f / 255.0f, 130.0f / 255.0f, 207.0f / 255.0f, 1.00f );
        colors[ImGuiCol_SliderGrab]       = ImVec4( 155.0f / 255.0f, 130.0f / 255.0f, 207.0f / 255.0f, 1.00f );
        colors[ImGuiCol_SliderGrabActive] = ImVec4( 185.0f / 255.0f, 160.0f / 255.0f, 237.0f / 255.0f, 1.00f );
        colors[ImGuiCol_Button]           = ImVec4( 0.20f, 0.25f, 0.29f, 1.00f );
        colors[ImGuiCol_ButtonHovered] = ImVec4( 0.20f, 0.25f, 0.29f, 1.00f ) + ImVec4( 0.1f, 0.1f, 0.1f, 0.1f );
        colors[ImGuiCol_ButtonActive]  = ImVec4( 0.20f, 0.25f, 0.29f, 1.00f ) + ImVec4( 0.1f, 0.1f, 0.1f, 0.1f );

        colors[ImGuiCol_Separator]        = ImVec4( 0.20f, 0.25f, 0.29f, 1.00f );
        colors[ImGuiCol_SeparatorHovered] = ImVec4( 0.10f, 0.40f, 0.75f, 0.78f );
        colors[ImGuiCol_SeparatorActive]  = ImVec4( 0.10f, 0.40f, 0.75f, 1.00f );

        colors[ImGuiCol_ResizeGrip]        = ImVec4( 0.26f, 0.59f, 0.98f, 0.25f );
        colors[ImGuiCol_ResizeGripHovered] = ImVec4( 0.26f, 0.59f, 0.98f, 0.67f );
        colors[ImGuiCol_ResizeGripActive]  = ImVec4( 0.26f, 0.59f, 0.98f, 0.95f );

        colors[ImGuiCol_PlotLines]             = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
        colors[ImGuiCol_PlotLinesHovered]      = ImVec4( 1.00f, 0.43f, 0.35f, 1.00f );
        colors[ImGuiCol_PlotHistogram]         = ImVec4( 0.90f, 0.70f, 0.00f, 1.00f );
        colors[ImGuiCol_PlotHistogramHovered]  = ImVec4( 1.00f, 0.60f, 0.00f, 1.00f );
        colors[ImGuiCol_TextSelectedBg]        = ImVec4( 0.26f, 0.59f, 0.98f, 0.35f );
        colors[ImGuiCol_DragDropTarget]        = ImVec4( 0.26f, 0.59f, 0.98f, 1.00f );
        colors[ImGuiCol_NavHighlight]          = ImVec4( 0.26f, 0.59f, 0.98f, 1.00f );
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
        colors[ImGuiCol_NavWindowingDimBg]     = ImVec4( 0.80f, 0.80f, 0.80f, 0.20f );
        colors[ImGuiCol_ModalWindowDimBg]      = ImVec4( 0.80f, 0.80f, 0.80f, 0.35f );

        colors[ImGuiCol_Header]        = TabActive + ImVec4( 0.1f, 0.1f, 0.1f, 0.1f );
        colors[ImGuiCol_HeaderHovered] = TabActive + ImVec4( 0.1f, 0.1f, 0.1f, 0.1f );
        colors[ImGuiCol_HeaderActive]  = TabActive + ImVec4( 0.05f, 0.05f, 0.05f, 0.1f );

#ifdef IMGUI_HAS_DOCK

        colors[ImGuiCol_Tab]                = TabUnactive;
        colors[ImGuiCol_TabHovered]         = TabActive + ImVec4( 0.1f, 0.1f, 0.1f, 0.1f );
        colors[ImGuiCol_TabActive]          = TabActive;
        colors[ImGuiCol_TabUnfocused]       = TabUnactive;
        colors[ImGuiCol_TabUnfocusedActive] = TabActive;
        colors[ImGuiCol_DockingEmptyBg]     = ImVec4( 0.33f, 0.33f, 0.33f, 1.00f );
        colors[ImGuiCol_DockingPreview]     = ImVec4( 0.33f, 0.33f, 0.33f, 1.00f );

#endif
    }

    void ThemeManager::SetBlackTheme()
    {
        static const float max = 255.0f;

        auto&   style  = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

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