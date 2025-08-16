#pragma once

#include <ImGui/imgui.h>
#include <string>
#include "IconsMaterialDesignIcons.hpp"

namespace Desert::Editor
{
    class EditorResources
    {
    public:
        static bool Initialize( const std::string& fontAwesomePath );

        static ImFont* GetIconFont()
        {
            return s_IconFont;
        }

        static bool IsInitialized()
        {
            return s_Initialized;
        }

        static ImFont* GetRegularFont()
        {
            return s_RegularFont;
        }

        static ImFont* GetBoldFont()
        {
            return s_BoldFont ? s_BoldFont : s_RegularFont;
        }

        static ImFont* GetExtraBoldFont()
        {
            return s_ExtraBoldFont ? s_ExtraBoldFont : s_RegularFont;
        }

    private:
        static bool    s_Initialized;
        static float   s_FontSize;
        static ImFont* s_RegularFont;
        static ImFont* s_BoldFont;
        static ImFont* s_ExtraBoldFont;
        static ImFont* s_IconFont;
    };

} // namespace Desert::Editor