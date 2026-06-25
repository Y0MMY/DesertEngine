#pragma once

#include "../IPanel.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include <stack>
#include <functional>
#include <memory>

namespace Desert::Assets
{
    class AssetManager;
}

namespace Desert::Editor
{
    namespace UI
    {
        class UIHelper;
    }
    class ThumbnailCache;

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

        // Lazily-resolved texture thumbnail handle (0 = none / not a registered texture). Cached so the
        // grid doesn't re-resolve every frame; resolution is existing-only (browsing never cooks).
        uint64_t ThumbnailHandle   = 0;
        bool     ThumbnailResolved = false;

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
        explicit FileExplorerPanel( const std::filesystem::path& rootPath,
                                    Assets::AssetManager*        assetManager = nullptr );
        ~FileExplorerPanel() override;
        void OnUIRender() override;
        void OnPreUpdate() override; // polls the current dir for external changes -> auto-refresh
        void OnEvent( Common::Event& e ) override; // OS file drop -> import into the current dir

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
        // Re-scan ONLY the current directory in place (keeps navigation; used by the watcher + QueueRefresh).
        void RefreshCurrentDirectory();
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

        Assets::AssetManager*           m_AssetManager = nullptr;
        std::unique_ptr<UI::UIHelper>   m_UIHelper;
        std::unique_ptr<ThumbnailCache> m_Thumbnails;

        // File watcher: cheap throttled poll of the current dir's entry signature -> QueueRefresh on change.
        int    m_PollCounter   = 0;
        size_t m_DirSignature  = 0;

        bool m_IsHovered = false; // is the Assets window hovered this frame (gates OS file-drop import)

        // Copy an external image into Resources/Textures, then import+register it (Import button).
        void ImportExternalTexture();
        // Copy one external file into the current dir; cook+register if it's a texture (drag-drop / import).
        void ImportExternalFile( const std::filesystem::path& src );
        // Resolve (existing-only) + draw a texture thumbnail for an entry; returns false if none.
        bool DrawTextureThumbnail( DirectoryInformation* entry, const ImVec2& size );
    };

} // namespace Desert::Editor