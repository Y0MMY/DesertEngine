#pragma once

#include "../IPanel.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include <stack>
#include <functional>

namespace Desert::Editor
{

    class FileExplorerPanel : public IPanel
    {
    public:
        FileExplorerPanel( const std::filesystem::path& rootPath )
             : IPanel( " FileExplorer" ), m_CurrentPath( rootPath )
        {
            RefreshCurrentDirectory();
        }

        void OnUIRender() override;

        void SetOnFileClickedCallback( std::function<void( const std::filesystem::path& )> callback )
        {
            m_FileClickedCallback = callback;
        }

    private:
        // UI Drawing functions
        void DrawNavigationBar();
        void DrawDirectoryContents();
        void DrawPathBreadcrumbs();
        void DrawEntryContextMenu( const std::filesystem::directory_entry& entry );
        void DrawContextMenu();

        // Utility functions
        const char* GetFileIcon( const std::filesystem::path& path );
        void        NavigateTo( const std::filesystem::path& path );
        void        RefreshCurrentDirectory();
        void        CreateNewFolder();
        void        CreateNewFile();
        void        HandleRenaming();
        void        CompleteRenaming( const char* newName );
        void        CancelRenaming();

    private:
        std::string                          m_PanelName;
        std::filesystem::path                m_RootPath;
        std::filesystem::path                m_CurrentPath;
        bool                                 m_ShowRenameModal   = false;
        char                                 m_RenameBuffer[256] = { 0 };
        std::filesystem::path                m_EntryToRename;
        std::filesystem::path                m_SelectedFile;

        std::stack<std::filesystem::path> m_PathHistory;
        std::stack<std::filesystem::path> m_ForwardHistory;

        std::vector<std::filesystem::directory_entry> m_Directories;
        std::vector<std::filesystem::directory_entry> m_Files;

        std::function<void( const std::filesystem::path& )> m_FileClickedCallback;
    };

} // namespace Desert::Editor