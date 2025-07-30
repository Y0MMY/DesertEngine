#include "FileExplorerPanel.hpp"
#include "../../Core/EditorResources.hpp"

namespace Desert::Editor
{
    void FileExplorerPanel::OnUIRender()
    {
        if ( !EditorResources::IsInitialized() )
        {
            ImGui::Text( "Font resources not loaded!" );
            return;
        }

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 8, 8 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 6 ) );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( 0.12f, 0.12f, 0.13f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.10f, 0.10f, 0.11f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( 0.2f, 0.2f, 0.25f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( 0.3f, 0.3f, 0.35f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( 0.25f, 0.25f, 0.3f, 1.0f ) );

        DrawNavigationBar();
        ImGui::Separator();

        DrawDirectoryContents();

        if ( ImGui::BeginPopupContextWindow( "FileExplorerContextMenu",
                                             ImGuiPopupFlags_MouseButtonRight |
                                                  ImGuiPopupFlags_NoOpenOverExistingPopup ) )
        {
            DrawContextMenu();
            ImGui::EndPopup();
        }

        HandleRenaming();

        ImGui::PopStyleColor( 5 );
        ImGui::PopStyleVar( 2 );
    }

    void FileExplorerPanel::DrawNavigationBar()
    {
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.3f, 0.3f, 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.2f, 0.2f, 0.2f, 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 4, 4 ) );

        if ( ImGui::Button( ICON_FA_HOME "##home" ) )
        {
            NavigateTo( m_RootPath );
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Go to project root" );
        ImGui::SameLine();

        ImGui::BeginDisabled( m_PathHistory.empty() );
        if ( ImGui::Button( ICON_FA_ARROW_LEFT "##back" ) )
        {
            m_ForwardHistory.push( m_CurrentPath );
            NavigateTo( m_PathHistory.top() );
            m_PathHistory.pop();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Go back" );
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled( m_ForwardHistory.empty() );
        if ( ImGui::Button( ICON_FA_ARROW_RIGHT "##forward" ) )
        {
            m_PathHistory.push( m_CurrentPath );
            NavigateTo( m_ForwardHistory.top() );
            m_ForwardHistory.pop();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Go forward" );
        ImGui::EndDisabled();
        ImGui::SameLine();

        ImGui::BeginDisabled( !m_CurrentPath.has_parent_path() );
        if ( ImGui::Button( ICON_FA_ARROW_UP "##up" ) )
        {
            NavigateTo( m_CurrentPath.parent_path() );
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Go up" );
        ImGui::EndDisabled();
        ImGui::SameLine();

        if ( ImGui::Button( ICON_FA_SYNC "##refresh" ) )
        {
            RefreshCurrentDirectory();
        }
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Refresh" );
        ImGui::SameLine();

        DrawPathBreadcrumbs();

        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 3 );
    }

    void FileExplorerPanel::DrawPathBreadcrumbs()
    {
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.3f, 0.3f, 0.3f, 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.2f, 0.2f, 0.2f, 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 2, 0 ) );

        if ( m_CurrentPath.empty() )
        {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor( 3 );
            return;
        }

        std::vector<std::filesystem::path> pathParts;
        std::filesystem::path              current;

        for ( const auto& part : m_CurrentPath )
        {
            if ( !part.empty() )
            {
                current /= part;
                pathParts.push_back( current );
            }
        }

        for ( size_t i = 0; i < pathParts.size(); ++i )
        {
            if ( i > 0 )
            {
                ImGui::SameLine();
                ImGui::TextUnformatted( ">" );
                ImGui::SameLine();
            }

            const auto& part = pathParts[i];
            if ( ImGui::Button( part.filename().string().c_str() ) )
            {
                NavigateTo( part );
            }
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 3 );
    }

    void FileExplorerPanel::DrawDirectoryContents()
    {
        const float footerHeight = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        if ( ImGui::BeginChild( "##directory_contents", ImVec2( 0, -footerHeight ), true,
                                ImGuiWindowFlags_AlwaysVerticalScrollbar ) )
        {
            // Search bar
            static char searchFilter[64] = "";
            ImGui::PushItemWidth( -1 );
            if ( ImGui::InputTextWithHint( "##search", ICON_FA_SEARCH " Search...", searchFilter,
                                           IM_ARRAYSIZE( searchFilter ) ) )
            {
            }
            ImGui::PopItemWidth();
            ImGui::Separator();

            if ( m_Directories.empty() && m_Files.empty() )
            {
                ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ), "Folder is empty" );
            }
            else
            {
                for ( const auto& entry : m_Directories )
                {
                    // Skip invalid entries
                    if ( entry.path().empty() )
                        continue;

                    const std::string id = "##dir_" + entry.path().filename().string();
                    ImGui::PushID( id.c_str() );

                    // Icon
                    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.8f, 0.8f, 1.0f, 1.0f ) );
                    ImGui::Text( ICON_FA_FOLDER );
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    // Name with selectable
                    if ( ImGui::Selectable( entry.path().filename().string().c_str(),
                                            m_SelectedFile == entry.path(), ImGuiSelectableFlags_AllowDoubleClick,
                                            ImVec2( 0, ImGui::GetTextLineHeight() * 1.5f ) ) )
                    {
                        if ( ImGui::IsMouseDoubleClicked( 0 ) )
                        {
                            NavigateTo( entry.path() );
                        }
                        m_SelectedFile = entry.path();
                    }

                    DrawEntryContextMenu( entry );
                    ImGui::PopID();
                }

                // Draw files
                for ( const auto& entry : m_Files )
                {
                    // Skip invalid entries
                    if ( entry.path().empty() )
                        continue;

                    const std::string id = "##file_" + entry.path().filename().string();
                    ImGui::PushID( id.c_str() );

                    // Icon
                    ImGui::Text( GetFileIcon( entry.path() ) );
                    ImGui::SameLine();

                    // Name with selectable
                    if ( ImGui::Selectable( entry.path().filename().string().c_str(),
                                            m_SelectedFile == entry.path(), ImGuiSelectableFlags_AllowDoubleClick,
                                            ImVec2( 0, ImGui::GetTextLineHeight() * 1.5f ) ) )
                    {
                        if ( ImGui::IsMouseDoubleClicked( 0 ) && m_FileClickedCallback )
                        {
                            m_FileClickedCallback( entry.path() );
                        }
                        m_SelectedFile = entry.path();
                    }

                    DrawEntryContextMenu( entry );
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();

        // Status bar
        ImGui::Separator();
        ImGui::Text( "%d items (%d %s, %d %s)", m_Directories.size() + m_Files.size(), m_Directories.size(),
                     m_Directories.size() == 1 ? "directory" : "directories", m_Files.size(),
                     m_Files.size() == 1 ? "file" : "files" );
    }

    const char* FileExplorerPanel::GetFileIcon( const std::filesystem::path& path )
    {
        static const std::unordered_map<std::string, const char*> iconMap = {
             { ".png", ICON_FA_FILE_IMAGE }, { ".jpg", ICON_FA_FILE_IMAGE }, { ".jpeg", ICON_FA_FILE_IMAGE },
             { ".tga", ICON_FA_FILE_IMAGE }, { ".bmp", ICON_FA_FILE_IMAGE }, { ".h", ICON_FA_FILE_CODE },
             { ".hpp", ICON_FA_FILE_CODE },  { ".c", ICON_FA_FILE_CODE },    { ".cpp", ICON_FA_FILE_CODE },
             { ".txt", ICON_FA_FILE_ALT },   { ".md", ICON_FA_FILE_ALT },    { ".json", ICON_FA_FILE_CODE },
             { ".xml", ICON_FA_FILE_CODE },  { ".yaml", ICON_FA_FILE_CODE }, { ".ini", ICON_FA_FILE_CODE } };

        const auto& ext = path.extension().string();
        auto        it  = iconMap.find( ext );
        return it != iconMap.end() ? it->second : ICON_FA_FILE;
    }

    void FileExplorerPanel::DrawEntryContextMenu( const std::filesystem::directory_entry& entry )
    {
        std::string popup_id = "##context_" + entry.path().filename().string();
        if ( ImGui::BeginPopupContextItem( popup_id.c_str() ) )
        {
            // Display the filename at the top of the context menu
            ImGui::TextDisabled( "%s", entry.path().filename().string().c_str() );
            ImGui::Separator();

            if ( ImGui::MenuItem( ICON_FA_PEN " Rename" ) )
            {
                m_EntryToRename = entry.path();
                strncpy( m_RenameBuffer, entry.path().filename().string().c_str(), sizeof( m_RenameBuffer ) );
                m_ShowRenameModal = true;
            }

            if ( ImGui::MenuItem( ICON_FA_TRASH " Delete" ) )
            {
                if ( entry.is_directory() )
                {
                    std::filesystem::remove_all( entry.path() );
                }
                else
                {
                    std::filesystem::remove( entry.path() );
                }
                RefreshCurrentDirectory();
            }

            if ( !entry.is_directory() && ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open in Explorer" ) )
            {
                // Platform-specific open in file explorer
            }

            ImGui::EndPopup();
        }
    }

    void FileExplorerPanel::DrawContextMenu()
    {
        if ( ImGui::MenuItem( ICON_FA_FOLDER_PLUS " New Folder" ) )
        {
            CreateNewFolder();
        }

        if ( ImGui::MenuItem( ICON_FA_FILE_ALT " New File" ) )
        {
            CreateNewFile();
        }

        ImGui::Separator();

        if ( ImGui::MenuItem( ICON_FA_SYNC " Refresh" ) )
        {
            RefreshCurrentDirectory();
        }

        if ( ImGui::MenuItem( ICON_FA_FOLDER_OPEN " Open in Explorer" ) )
        {
            // Platform-specific open in file explorer
        }
    }

    void FileExplorerPanel::NavigateTo( const std::filesystem::path& path )
    {
        if ( path == m_CurrentPath )
            return;

        m_PathHistory.push( m_CurrentPath );
        m_CurrentPath = path;
        RefreshCurrentDirectory();
    }

    void FileExplorerPanel::RefreshCurrentDirectory()
    {
        m_Directories.clear();
        m_Files.clear();

        try
        {
            if ( std::filesystem::exists( m_CurrentPath ) )
            {
                for ( const auto& entry : std::filesystem::directory_iterator( m_CurrentPath ) )
                {
                    try
                    {
                        if ( entry.is_directory() )
                        {
                            m_Directories.push_back( entry );
                        }
                        else if ( entry.is_regular_file() )
                        {
                            m_Files.push_back( entry );
                        }
                    }
                    catch ( const std::exception& e )
                    {
                        // Skip invalid entries
                        continue;
                    }
                }
            }
        }
        catch ( const std::exception& e )
        {
        }

        // Sort entries alphabetically
        std::sort( m_Directories.begin(), m_Directories.end(), []( const auto& a, const auto& b )
                   { return a.path().filename().string() < b.path().filename().string(); } );

        std::sort( m_Files.begin(), m_Files.end(), []( const auto& a, const auto& b )
                   { return a.path().filename().string() < b.path().filename().string(); } );
    }

    void FileExplorerPanel::CreateNewFolder()
    {
        std::string newFolderName = "New Folder";
        int         counter       = 1;
        while ( std::filesystem::exists( m_CurrentPath / newFolderName ) )
        {
            newFolderName = std::format( "New Folder ({})", counter++ );
        }

        std::filesystem::create_directory( m_CurrentPath / newFolderName );
        RefreshCurrentDirectory();
    }

    void FileExplorerPanel::CreateNewFile()
    {
        std::string newFileName = "New File.txt";
        int         counter     = 1;
        while ( std::filesystem::exists( m_CurrentPath / newFileName ) )
        {
            newFileName = std::format( "New File ({}.txt)", counter++ );
        }

        /* std::ofstream out( m_CurrentPath / newFileName );
         out.close();*/
        RefreshCurrentDirectory();
    }

    void FileExplorerPanel::HandleRenaming()
    {
        if ( !m_ShowRenameModal )
            return;

        ImGui::OpenPopup( "Rename" );

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos( center, ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
        ImGui::SetNextWindowSize( ImVec2( 400, 0 ), ImGuiCond_Always );

        if ( ImGui::BeginPopupModal( "Rename", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove ) )
        {
            ImGui::Text( "Rename '%s' to:", m_EntryToRename.filename().string().c_str() );
            ImGui::Spacing();

            ImGui::SetNextItemWidth( -1 );
            bool renameConfirmed =
                 ImGui::InputText( "##rename", m_RenameBuffer, IM_ARRAYSIZE( m_RenameBuffer ),
                                   ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll );

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            float buttonWidth = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * 0.5f;

            if ( ImGui::Button( "Cancel", ImVec2( buttonWidth, 0 ) ) )
            {
                m_ShowRenameModal = false;
            }

            ImGui::SameLine();

            if ( ImGui::Button( "Rename", ImVec2( buttonWidth, 0 ) ) || renameConfirmed )
            {
                CompleteRenaming( m_RenameBuffer );
            }

            if ( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
            {
                m_ShowRenameModal = false;
            }

            ImGui::EndPopup();
        }
    }

    void FileExplorerPanel::CompleteRenaming( const char* newName )
    {
        if ( strlen( newName ) == 0 )
        {
            m_ShowRenameModal = false;
            return;
        }

        try
        {
            auto newPath = m_EntryToRename.parent_path() / newName;

            // Skip if name didn't change
            if ( newPath == m_EntryToRename )
            {
                m_ShowRenameModal = false;
                return;
            }

            // Check if new name already exists
            if ( std::filesystem::exists( newPath ) )
            {
                return;
            }

            std::filesystem::rename( m_EntryToRename, newPath );

            if ( m_SelectedFile == m_EntryToRename )
            {
                m_SelectedFile = newPath;
            }

            CancelRenaming();
            RefreshCurrentDirectory();
        }
        catch ( const std::exception& e )
        {
        }
    }

    void FileExplorerPanel::CancelRenaming()
    {
        m_ShowRenameModal = false;
    }

} // namespace Desert::Editor