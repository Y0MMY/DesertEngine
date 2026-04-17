#pragma once

#include "../IPanel.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include <stack>
#include <functional>

namespace Desert::Editor
{
    enum class FileType
    {
        Unknown = 0,
        Scene,
        Prefab,
        Script,
        Audio,
        Shader,
        Texture,
        Cubemap,
        Model,
        Material,
        Project,
        Ini,
        Font
    };

    struct DirectoryInformation
    {
        DirectoryInformation*              Parent;
        std::vector<DirectoryInformation*> Children;

        std::string AssetPath;
        // SharedPtr<Graphics::Texture2D> Thumbnail = nullptr;
        FileType Type;
        uint64_t FileSize;
        ImVec4   FileTypeColour;

        bool Hidden = false;
        bool IsFile = true;
        bool Opened = false;
        bool Leaf   = true;

    public:
        DirectoryInformation( const std::string& path, bool isFile )
        {
            AssetPath = path;
            IsFile    = isFile;
            Hidden    = false;
        }

        ~DirectoryInformation()
        {
        }
    };
    class FileExplorerPanel : public IPanel
    {
    public:
        explicit FileExplorerPanel( const std::filesystem::path& rootPath );
        void OnUIRender() override;

        bool RenderFile( int dirIndex, bool folder, int shownIndex, bool gridView );
        void DrawFolder( DirectoryInformation* dirInfo, bool defaultOpen = false );

        void DestroyGraphicsResources()
        {
            /* m_FolderIcon.reset();
             m_FileIcon.reset();
             m_Directories.clear();*/
        }

        std::string ProcessDirectory( const std::string& directoryPath, DirectoryInformation* parent,
                                      bool processChildren );

        void ChangeDirectory( DirectoryInformation* directory );
        void RemoveDirectory( DirectoryInformation* directory, bool removeFromParent = true );
        // void OnNewProject() override;
        void Refresh();
        void QueueRefresh()
        {
            m_Refresh = true;
        }

    private:
        void CreateThumbnailPath( DirectoryInformation* directoryInfo, std::string& assetPath,
                                  std::string& AbsolutePath );

    private:
        std::filesystem::path m_CurrentPath;

        float       m_MinGridSize = 50;
        float       m_MaxGridSize = 400;
        std::string m_MovePath;
        std::string m_LastNavPath;
        std::string m_Delimiter;

        size_t m_BasePathLen;
        bool   m_IsDragging;
        bool   m_IsInListView;
        bool   m_UpdateBreadCrumbs;
        bool   m_ShowHiddenFiles;
        int    m_GridItemsPerRow;
        float  m_GridSize = 360.0f;

        ImGuiTextFilter m_Filter;

        bool m_TextureCreated = false;

        std::string m_BasePath;
        std::string m_AssetPath;

        bool m_Refresh = false;

        bool m_UpdateNavigationPath = true;

        DirectoryInformation* m_CurrentDir;
        DirectoryInformation* m_BaseProjectDir;
        DirectoryInformation* m_NextDirectory;
        DirectoryInformation* m_PreviousDirectory;

        std::unordered_map<std::string, std::shared_ptr<DirectoryInformation>> m_Directories;
        std::vector<DirectoryInformation*>                                     m_BreadCrumbData;
        /* TDArray<DirectoryInformation*>                                                     m_BreadCrumbData;
         SharedPtr<Graphics::Texture2D>                                                     m_FolderIcon;
         SharedPtr<Graphics::Texture2D>                                                     m_FileIcon;*/

        DirectoryInformation* m_CurrentSelected;

        std::string m_RequestedThumbnailPath;
        std::string m_CopiedPath;
        bool        m_CutFile = false;
    };

} // namespace Desert::Editor