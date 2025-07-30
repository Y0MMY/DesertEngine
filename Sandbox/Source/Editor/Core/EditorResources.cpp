#include "EditorResources.hpp"

namespace Desert::Editor
{
    bool    EditorResources::s_Initialized = false;
    ImFont* EditorResources::s_IconFont    = nullptr;

    bool EditorResources::Initialize( const std::string& fontAwesomePath )
    {
        if ( s_Initialized )
            return true;

        ImGuiIO& io = ::ImGui::GetIO();

        ImFont* mainFont = io.Fonts->AddFontFromFileTTF( "Resources/Fonts/Roboto-Black.ttf", 16.0f, nullptr,
                                                         io.Fonts->GetGlyphRangesCyrillic() );

        if ( !mainFont )
        {
            mainFont = io.Fonts->AddFontDefault();
        }
        io.FontDefault = mainFont;

        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH  = true;

        static const ImWchar iconsRanges[] = { 0xf000, 0xf8ff, 0 }; 

        ImFontConfig iconsConfig;
        iconsConfig.MergeMode        = true;
        iconsConfig.PixelSnapH       = true;
        iconsConfig.GlyphMinAdvanceX = 14.0f;
        iconsConfig.GlyphOffset.y    = 1.0f;

        s_IconFont = io.Fonts->AddFontFromFileTTF( fontAwesomePath.c_str(), 14.0f, &iconsConfig, iconsRanges );

        s_Initialized = ( s_IconFont != nullptr );
        return s_Initialized;
    }
} // namespace Desert::Editor