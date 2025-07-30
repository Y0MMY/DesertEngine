#pragma once

#include <ImGui/imgui.h>
#include <string>
#include "FontAwesomeDefinitions.hpp"

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

    private:
        static bool    s_Initialized;
        static ImFont* s_IconFont;
    };

    namespace Icons
    {
        // File System
        constexpr const char* Home       = ICON_FA_HOME;
        constexpr const char* ArrowLeft  = ICON_FA_ARROW_LEFT;
        constexpr const char* ArrowRight = ICON_FA_ARROW_RIGHT;
        constexpr const char* ArrowUp    = ICON_FA_ARROW_UP;
        constexpr const char* Sync       = ICON_FA_REFRESH;
        constexpr const char* Folder     = ICON_FA_FOLDER;
        constexpr const char* File       = ICON_FA_FILE;
        constexpr const char* FileAlt    = ICON_FA_FILE_O;
        constexpr const char* FileCode   = ICON_FA_FILE_CODE_O;
        constexpr const char* FileImage  = ICON_FA_FILE_IMAGE_O;
    } // namespace Icons

} // namespace Desert::Editor