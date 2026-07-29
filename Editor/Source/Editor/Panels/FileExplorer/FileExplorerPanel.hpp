#pragma once

#include "../IPanel.hpp"
#include <Common/Utilities/FileSystem.hpp>
#include <ImGui/imgui.h>
#include <stack>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

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
    class AssetThumbnailRenderer;
}

namespace Desert::Core
{
    class Scene; // for "Capture Thumbnail from viewport" (reads the main scene's rendered final image)
}

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
        ShaderGraph,
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
        uint64_t LastWriteTime = 0; // filesystem mtime (for "sort by date"); cached so sorting needs no syscalls
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
                                    Assets::AssetManager*        assetManager  = nullptr,
                                    std::weak_ptr<::Desert::Core::Scene> viewportScene = {} );
        ~FileExplorerPanel() override;
        void OnUIRender() override;
        void OnPreUpdate() override; // polls the current dir for external changes -> auto-refresh
        void OnEvent( Common::Event& e ) override; // OS file drop -> import into the current dir

        bool RenderFile( int dirIndex, bool folder, int shownIndex, bool gridView );
        // Right-click context menu on a file/folder: Open (default app), Show in Explorer, Open folder, etc.
        void DrawItemContextMenu( DirectoryInformation& entry );
        // Modal dialogs for the cross-platform file ops (rename / delete-with-reference-warning).
        void DrawFileOpsPopups();
        // Multi-select click handling (plain / Ctrl-toggle / Shift-range over the display order).
        void SelectClick( DirectoryInformation* entry, int shownIndex );
        bool IsSelected( const DirectoryInformation* entry ) const;
        // Drag source with a thumbnail/big-icon preview (needs the thumbnail cache, hence a member).
        void EmitAssetDragSource( const DirectoryInformation& entry );

        // True while any tile (card rect / list row) is hovered this frame — clicking elsewhere in the
        // body clears the selection (the ScrollY table is a child window, so an item-based check can't work).
        bool m_TileHovered = false;
        // Paths of the current multi-selection; falls back to m_CurrentSelected when empty.
        std::vector<std::string> SelectionPaths() const;
        // Cut/copy/paste of the current selection into the current directory.
        void PasteClipboard();

        // Phase-3 navigation.
        void        GoBack();
        void        GoForward();
        void        NavigateToPath( const std::string& path ); // resolves a visited path to its dir
        void        ToggleFavorite( const std::string& folderPath );
        bool        IsFavorite( const std::string& folderPath ) const;
        void        LoadFavorites();
        void        SaveFavorites() const;
        std::string FavoritesFile() const;

        // Phase-4 engine integration: instantiate a prefab into the open scene; create a new material asset.
        void AddPrefabToScene( const std::string& prefabPath );
        void CreateNewMaterial();
        // UE-style "Capture Thumbnail": grab the current main-viewport rendered image, center-crop to a
        // square, downscale, and save it AS this asset's thumbnail (same DiskPath key the grid reads). Lets
        // the user frame the asset in the scene and use that exact view as the preview.
        void CaptureThumbnailFromViewport( const std::string& assetPath );
        // Filtered (m_SearchBuf) + sorted (m_SortMode) child indices for the current directory.
        std::vector<size_t> BuildDisplayOrder() const;
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

        // Content-Browser left pane (folder tree + favorites) width; dragged via the splitter, remembered
        // for the session. Clamped to [kMinTreeWidth, avail - kMinContentWidth] each frame.
        float m_TreeWidth = 240.0f;

        // Asset filtering + sorting (toolbar).
        enum class SortMode
        {
            Name = 0,
            DateModified,
            Type,
            Size
        };
        char     m_SearchBuf[128] = { 0 };
        SortMode m_SortMode        = SortMode::Name;
        bool     m_SortDescending  = false;

        ImGuiTextFilter m_Filter;

        bool m_TextureCreated = false;

        std::string m_BasePath;
        std::string m_AssetPath;

        bool m_Refresh = false;

        bool m_UpdateNavigationPath = true;

        // Default-initialized: the ctor omitted m_CurrentSelected / m_NextDirectory, so they held
        // indeterminate values and the first frame could deref garbage (a layout-dependent crash in
        // OnUIRender). Also nulled on Refresh so a rebuilt m_Directories never leaves a dangling selection.
        DirectoryInformation* m_CurrentDir        = nullptr;
        DirectoryInformation* m_BaseProjectDir    = nullptr;
        DirectoryInformation* m_NextDirectory     = nullptr;
        DirectoryInformation* m_PreviousDirectory = nullptr;

        std::unordered_map<std::string, std::shared_ptr<DirectoryInformation>> m_Directories;
        std::vector<DirectoryInformation*>                                     m_BreadCrumbData;
        /* TDArray<DirectoryInformation*>                                                     m_BreadCrumbData;
         SharedPtr<Graphics::Texture2D>                                                     m_FolderIcon;
         SharedPtr<Graphics::Texture2D>                                                     m_FileIcon;*/

        DirectoryInformation* m_CurrentSelected = nullptr;

        std::string m_RequestedThumbnailPath;
        std::string m_CopiedPath;
        bool        m_CutFile = false;

        // Phase-1 file operations (rename / delete / duplicate / move), cross-platform.
        bool                     m_ShowRenamePopup   = false;
        std::string              m_RenamePath;
        char                     m_RenameBuf[128]    = { 0 };
        bool                     m_ShowDeleteConfirm = false;
        std::vector<std::string> m_PendingDeleteList; // paths queued for the delete-confirm modal
        std::vector<std::string> m_DeleteReferencers; // assets still pointing at the delete target(s)
        std::string              m_FileOpStatus;      // last error line (shown in the toolbar area)

        // Phase-2 multi-select + clipboard.
        std::unordered_set<std::string> m_Selection;            // selected asset paths
        int                             m_SelectionAnchorShown = -1; // display index of the range anchor
        std::vector<std::string>        m_Clipboard;            // cut/copied paths
        bool                            m_ClipboardCut = false; // true = move on paste, false = copy

        // Phase-3 navigation/UX.
        int                      m_TypeFilter = -1;      // FileType value to show, or -1 for "All"
        std::vector<std::string> m_NavHistory;           // visited folder paths (back/forward)
        int                      m_NavPos            = -1;
        bool                     m_NavigatingHistory = false; // suppress history push during back/forward
        std::vector<std::string> m_Favorites;                 // pinned folder paths (persisted)

        Assets::AssetManager*           m_AssetManager = nullptr;
        std::unique_ptr<UI::UIHelper>   m_UIHelper;
        std::unique_ptr<ThumbnailCache>          m_Thumbnails;
        std::unique_ptr<AssetThumbnailRenderer>  m_ThumbRenderer; // lazily created (needs the device ready)
        std::weak_ptr<::Desert::Core::Scene>     m_ViewportScene; // for "Capture Thumbnail from viewport"
        std::unordered_set<std::string>          m_FailedThumbs;  // assets that failed to load -> show icon, no retry spam

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
        // Draw a rendered preview for a material entry (material-on-sphere). Generates the PNG lazily
        // (throttled to ~1/frame) on first use and caches it to disk; returns false until the PNG exists.
        bool DrawRenderedMaterialThumbnail( DirectoryInformation* entry, const ImVec2& size );
        // Same, for a mesh entry (the mesh auto-framed by its bounds).
        bool DrawRenderedMeshThumbnail( DirectoryInformation* entry, const ImVec2& size );

        // Bottom preview strip for the currently selected file: thumbnail (texture/material/mesh) or a
        // text excerpt (scripts, .demat/.deprefab/.desce JSON), plus name/type/size.
        void DrawPreviewPane();

        // Text-excerpt cache for the preview pane (loaded once per selection change, capped size).
        std::string m_PreviewTextPath;
        std::string m_PreviewText;
    };

} // namespace Desert::Editor