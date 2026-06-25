#include "EditorResources.hpp"

namespace Desert::Editor
{
    bool    EditorResources::s_Initialized = false;
    ImFont* EditorResources::s_IconFont    = nullptr;
    ImFont* EditorResources::s_RegularFont = nullptr;
    ImFont* EditorResources::s_ExtraBoldFont = nullptr;
    ImFont* EditorResources::s_BoldFont    = nullptr;
    ImFont* EditorResources::s_BigIconFont = nullptr;

    bool EditorResources::Initialize( const std::string& fontAwesomePath )
    {
        if ( s_Initialized )
            return true;

        ImGuiIO& io = ::ImGui::GetIO();

        s_BoldFont = io.Fonts->AddFontFromFileTTF( "Resources/Fonts/Roboto-Bold.ttf", 16.0F, nullptr,
                                                   io.Fonts->GetGlyphRangesCyrillic() );


        s_ExtraBoldFont = io.Fonts->AddFontFromFileTTF( "Resources/Fonts/Roboto-Bold.ttf", 22.0F, nullptr,
                                                   io.Fonts->GetGlyphRangesCyrillic() );


        s_RegularFont = io.Fonts->AddFontFromFileTTF( "Resources/Fonts/Roboto-Regular.ttf", 18.0F, nullptr,
                                                         io.Fonts->GetGlyphRangesCyrillic() );

        if ( !s_RegularFont)
        {
            s_RegularFont = io.Fonts->AddFontDefault();
        }
        io.FontDefault = s_RegularFont;

        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH  = true;

        static const ImWchar iconsRanges[] = { ICON_MIN_MDI, ICON_MAX_MDI, 0 };

        ImFontConfig iconsConfig;
        iconsConfig.MergeMode   = true;
        iconsConfig.PixelSnapH  = true;
        iconsConfig.OversampleH = iconsConfig.OversampleV = 1;
        iconsConfig.GlyphMinAdvanceX                      = 4.0f;
        iconsConfig.SizePixels                            = 16.0f;
        iconsConfig.GlyphOffset.y                         = 1.0f;


        s_IconFont = io.Fonts->AddFontFromFileTTF( fontAwesomePath.c_str(), 16.0f, &iconsConfig, iconsRanges );

        // Large, standalone (non-merged) MDI font for big asset-grid icons.
        ImFontConfig bigIconConfig;
        bigIconConfig.MergeMode       = false;
        bigIconConfig.PixelSnapH      = true;
        bigIconConfig.OversampleH     = bigIconConfig.OversampleV = 1;
        bigIconConfig.GlyphMinAdvanceX                            = 48.0f;
        s_BigIconFont = io.Fonts->AddFontFromFileTTF( fontAwesomePath.c_str(), 48.0f, &bigIconConfig, iconsRanges );

        s_Initialized = ( s_IconFont != nullptr );
        return s_Initialized;
    }
} // namespace Desert::Editor