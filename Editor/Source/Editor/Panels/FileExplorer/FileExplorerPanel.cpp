#define IMGUI_DEFINE_MATH_OPERATORS

#include "FileExplorerPanel.hpp"
#include "../../Core/EditorResources.hpp"

#include <ImGui/imgui_internal.h>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        bool MoveFile( const std::string& filePath, const std::string& movePath )
        {
            std::string s = "move " + filePath + " " + movePath;

#ifdef DESERT_PLATFORM_WINDOWS
            system( s.c_str() );
#endif

            return false;
        }
    } // namespace

    static const std::unordered_map<FileType, std::string> s_FileTypesToString = {
         { FileType::Unknown, "Unknown" }, { FileType::Scene, "Scene" },       { FileType::Prefab, "Prefab" },
         { FileType::Script, "Script" },   { FileType::Shader, "Shader" },     { FileType::Texture, "Texture" },
         { FileType::Font, "Font" },       { FileType::Cubemap, "Cubemap" },   { FileType::Model, "Model" },
         { FileType::Audio, "Audio" },     { FileType::Material, "Material" },
    };

    static const std::unordered_map<std::string, FileType> s_FileTypes = {
         { "lsn", FileType::Scene },   { "lprefab", FileType::Prefab }, { "cs", FileType::Script },
         { "lua", FileType::Script },  { "glsl", FileType::Shader },    { "shader", FileType::Shader },
         { "frag", FileType::Shader }, { "vert", FileType::Shader },    { "comp", FileType::Shader },
         { "png", FileType::Texture }, { "jpg", FileType::Texture },    { "jpeg", FileType::Texture },
         { "bmp", FileType::Texture }, { "gif", FileType::Texture },    { "tga", FileType::Texture },
         { "ttf", FileType::Font },    { "hdr", FileType::Cubemap },    { "obj", FileType::Model },
         { "fbx", FileType::Model },   { "gltf", FileType::Model },     { "glb", FileType::Model },
         { "mp3", FileType::Audio },   { "m4a", FileType::Audio },      { "wav", FileType::Audio },
         { "ogg", FileType::Audio },   { "lmat", FileType::Material },
    };

    static const std::unordered_map<FileType, ImVec4> s_TypeColors = {
         { FileType::Scene, { 0.8f, 0.4f, 0.22f, 1.00f } },
         { FileType::Prefab, { 0.10f, 0.50f, 0.80f, 1.00f } },
         { FileType::Script, { 0.10f, 0.50f, 0.80f, 1.00f } },
         { FileType::Font, { 0.60f, 0.19f, 0.32f, 1.00f } },
         { FileType::Shader, { 0.10f, 0.50f, 0.80f, 1.00f } },
         { FileType::Texture, { 0.82f, 0.20f, 0.33f, 1.00f } },
         { FileType::Cubemap, { 0.82f, 0.18f, 0.30f, 1.00f } },
         { FileType::Model, { 0.18f, 0.82f, 0.76f, 1.00f } },
         { FileType::Audio, { 0.20f, 0.80f, 0.50f, 1.00f } },
    };

    static const std::unordered_map<FileType, const char*> s_FileTypesToIcon = {
         { FileType::Unknown, ICON_MDI_FILE },
         { FileType::Scene, ICON_MDI_FILE },
         { FileType::Prefab, ICON_MDI_FILE },
         { FileType::Script, ICON_MDI_LANGUAGE_LUA },
         { FileType::Shader, ICON_MDI_IMAGE_FILTER_BLACK_WHITE },
         { FileType::Texture, ICON_MDI_FILE_IMAGE },
         { FileType::Font, ICON_MDI_CARD_TEXT },
         { FileType::Cubemap, ICON_MDI_IMAGE_FILTER_HDR },
         { FileType::Model, ICON_MDI_VECTOR_POLYGON },
         { FileType::Audio, ICON_MDI_MICROPHONE },
    };

    FileExplorerPanel::FileExplorerPanel( const std::filesystem::path& rootPath )
         : IPanel( "Assets" ), m_CurrentPath( rootPath ), m_CurrentDir( nullptr ),
           m_BaseProjectDir( nullptr ), m_PreviousDirectory( nullptr ), m_GridSize( 120.0f ),
           m_MinGridSize( 40.0f ), m_MaxGridSize( 400.0f ), m_IsInListView( false ), m_IsDragging( false ),
           m_ShowHiddenFiles( false ), m_UpdateNavigationPath( true ), m_Refresh( false )
    {
#ifdef DESERT_PLATFORM_WINDOWS
        m_Delimiter = std::string( "\\" );
#else
        m_Delimiter = std::string( "/" );
#endif
        float dpi  = 1.0;
        m_GridSize = 120.0f;
        m_GridSize *= dpi;
        m_MinGridSize = 40.0f;
        m_MaxGridSize = 400.0f;
        m_MinGridSize *= dpi;
        m_MaxGridSize *= dpi;
        m_BasePath = m_CurrentPath.string();

#ifdef DESERT_PLATFORM_WINDOWS
        m_Delimiter = "\\";
#else
        m_Delimiter = "/";
#endif

        if ( rootPath.empty() )
        {
            m_BasePath = "Assets"; 
        }
        else
        {
            m_BasePath = rootPath.string();
        }

        std::string baseDirectoryHandle = ProcessDirectory( m_BasePath, nullptr, true );

        if ( m_Directories.find( baseDirectoryHandle ) != m_Directories.end() )
        {
            m_BaseProjectDir = m_Directories[baseDirectoryHandle].get();
            m_CurrentDir     = m_BaseProjectDir;

            if ( m_BaseProjectDir )
            {
                ChangeDirectory( m_BaseProjectDir );
            }
        }
    }

    void FileExplorerPanel::ChangeDirectory( DirectoryInformation* directory )
    {
        if ( !directory )
            return;

        m_PreviousDirectory    = m_CurrentDir;
        m_CurrentDir           = directory;
        m_UpdateNavigationPath = true;

        if ( !m_CurrentDir->Opened )
        {
            ProcessDirectory( m_CurrentDir->AssetPath, m_CurrentDir->Parent, true );
        }
    }

    void FileExplorerPanel::RemoveDirectory( DirectoryInformation* directory, bool removeFromParent )
    {
        if ( directory->Parent && removeFromParent )
        {
            directory->Parent->Children.clear();
        }

        for ( auto& subdir : directory->Children )
            RemoveDirectory( subdir, false );

        m_Directories.erase( directory->AssetPath );
    }

    bool IsHidden( const std::filesystem::path& filePath )
    {
        try
        {
            std::filesystem::file_status status = std::filesystem::status( filePath );
            std::string                  name   = filePath.stem().string();
            return ( status.permissions() & std::filesystem::perms::owner_read ) == std::filesystem::perms::none ||
                   name == ".DS_Store";
        }
        catch ( const std::filesystem::filesystem_error& ex )
        {
            // LOG_ERROR( "Error accessing file: %s", ex.what() );
        }

        return false; // Return false by default if any error occurs
    }

    std::string FileExplorerPanel::ProcessDirectory( const std::string&    directoryPath,
                                                     DirectoryInformation* parent, bool processChildren )
    {
        const auto& directory = m_Directories[directoryPath];
        if ( directory && directory->Opened )
            return directory->AssetPath;

        std::string absolutePath = directoryPath; // Simplified - replace with actual path resolution
        auto        stdPath      = std::filesystem::path( absolutePath );

        std::shared_ptr<DirectoryInformation> directoryInfo =
             directory ? directory
                       : std::make_shared<DirectoryInformation>( directoryPath,
                                                                 !std::filesystem::is_directory( stdPath ) );
        directoryInfo->Parent = parent;

        // TODO: create paths at max size and use free list
        directoryInfo->AssetPath = directoryPath; // Simplified

        std::string extension = stdPath.extension().string();
        if ( !extension.empty() && extension[0] == '.' )
            extension = extension.substr( 1 );

        if ( std::filesystem::is_directory( stdPath ) )
        {
            directoryInfo->IsFile = false;
            directoryInfo->Leaf   = true;
            for ( auto& entry : std::filesystem::directory_iterator( stdPath ) )
            {
                if ( !m_ShowHiddenFiles && IsHidden( entry.path() ) )
                {
                    continue;
                }

                if ( directoryInfo->AssetPath.find( "//Assets/Cache" ) != std::string::npos )
                {
                    directoryInfo->Hidden = true;
                    continue;
                }

                if ( entry.is_directory() )
                    directoryInfo->Leaf = false;

                if ( processChildren )
                {
                    directoryInfo->Opened = true;

                    std::string subdirHandle =
                         ProcessDirectory( entry.path().generic_string(), directoryInfo.get(), false );
                    directoryInfo->Children.push_back( m_Directories[subdirHandle].get() );
                }
            }
        }
        else
        {
            auto        fileType   = FileType::Unknown;
            const auto& fileTypeIt = s_FileTypes.find( extension );
            if ( fileTypeIt != s_FileTypes.end() )
                fileType = fileTypeIt->second;

            directoryInfo->IsFile = true;
            directoryInfo->Type   = fileType;
            directoryInfo->FileSize =
                 std::filesystem::exists( stdPath ) ? std::filesystem::file_size( stdPath ) : 0;
            directoryInfo->Hidden = std::filesystem::exists( stdPath ) ? IsHidden( stdPath ) : true;
            directoryInfo->Opened = true;
            directoryInfo->Leaf   = true;

            ImVec4      fileTypeColor   = { 1.0f, 1.0f, 1.0f, 1.0f };
            const auto& fileTypeColorIt = s_TypeColors.find( fileType );
            if ( fileTypeColorIt != s_TypeColors.end() )
                fileTypeColor = fileTypeColorIt->second;

            directoryInfo->FileTypeColour = fileTypeColor;
        }

        if ( !directory )
            m_Directories[directoryInfo->AssetPath] = directoryInfo;
        return directoryInfo->AssetPath;
    }

    void FileExplorerPanel::DrawFolder( DirectoryInformation* dirInfo, bool defaultOpen )
    {
        ImGuiTreeNodeFlags nodeFlags = ( ( dirInfo == m_CurrentDir ) ? ImGuiTreeNodeFlags_Selected : 0 );
        nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

        if ( dirInfo->Parent == nullptr )
            nodeFlags |= ImGuiTreeNodeFlags_Framed;

        const ImColor TreeLineColor = ImColor( 128, 128, 128, 128 );
        const float   SmallOffsetX  = 6.0f; // * Application::Get().GetWindowDPI();
        ImDrawList*   drawList      = ImGui::GetWindowDrawList();

        if ( !dirInfo->IsFile )
        {
            if ( dirInfo->Leaf )
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;

            if ( defaultOpen )
                nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf;

            nodeFlags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

            bool isOpen = ImGui::TreeNodeEx( (void*)(intptr_t)( dirInfo ), nodeFlags, "" );
            if ( ImGui::IsItemClicked() )
            {
                ChangeDirectory( dirInfo );
            }

            const char* folderIcon = ( ( isOpen && !dirInfo->Leaf ) || m_CurrentDir == dirInfo )
                                          ? ICON_MDI_FOLDER_OPEN
                                          : ICON_MDI_FOLDER;
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ) ); // Replace with actual color
            ImGui::TextUnformatted( folderIcon );
            ImGui::PopStyleColor();
            ImGui::SameLine();

            std::string fileName = std::filesystem::path( dirInfo->AssetPath ).filename().string();
            ImGui::TextUnformatted( fileName.c_str() );

            ImVec2 verticalLineStart = ImGui::GetCursorScreenPos();

            if ( isOpen && !dirInfo->Leaf )
            {
                verticalLineStart.x += SmallOffsetX; // to nicely line up with the arrow symbol
                ImVec2 verticalLineEnd = verticalLineStart;

                for ( size_t i = 0; i < dirInfo->Children.size(); i++ )
                {
                    if ( !m_ShowHiddenFiles && dirInfo->Children[i]->Hidden )
                    {
                        continue;
                    }

                    if ( !dirInfo->Children[i]->IsFile )
                    {
                        auto currentPos = ImGui::GetCursorScreenPos();

                        ImGui::Indent( 10.0f );

                        float HorizontalTreeLineSize =
                             16.0f; // * Application::Get().GetWindowDPI(); // chosen arbitrarily

                        if ( !dirInfo->Children[i]->Leaf )
                            HorizontalTreeLineSize *= 0.5f;
                        DrawFolder( dirInfo->Children[i] );

                        const ImRect childRect =
                             ImRect( currentPos, currentPos + ImVec2( 0.0f, ImGui::GetFontSize() ) );

                        const float midpoint = ( childRect.Min.y + childRect.Max.y ) * 0.5f;
                        drawList->AddLine( ImVec2( verticalLineStart.x, midpoint ),
                                           ImVec2( verticalLineStart.x + HorizontalTreeLineSize, midpoint ),
                                           TreeLineColor );
                        verticalLineEnd.y = midpoint;

                        ImGui::Unindent( 10.0f );
                    }
                }

                drawList->AddLine( verticalLineStart, verticalLineEnd, TreeLineColor );

                ImGui::TreePop();
            }

            if ( isOpen && dirInfo->Leaf )
                ImGui::TreePop();
        }

        if ( m_IsDragging && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) )
        {
            m_MovePath = dirInfo->AssetPath;
        }
    }

    static int FileIndex = 0;

    ImVec2 GetAspectCorrectedSize( const ImVec2& originalSize, float maxSize )
    {
        float aspect = originalSize.x / originalSize.y;
        if ( aspect > 1.0f )
            return { maxSize, maxSize / aspect }; // Wider than tall
        else
            return { maxSize * aspect, maxSize }; // Taller than wide or square
    }

    void FileExplorerPanel::OnUIRender()
    {
        {
            FileIndex              = 0;
            auto        windowSize = ImGui::GetWindowSize();
            bool        vertical   = windowSize.y > windowSize.x;
            static bool Init       = false;

            if ( m_Refresh )
            {
                Refresh();
                m_Refresh = false;
            }

            if ( !vertical )
            {
                // Replace with columns implementation
                ImGui::Columns( 2, "FileExplorerPanelColumns", false );
                if ( !Init )
                {
                    ImGui::SetColumnWidth( 0, ImGui::GetWindowContentRegionMax().x / 3.0f );
                    Init = true;
                }
                ImGui::BeginChild( "##folders_common" );
            }
            else
                ImGui::BeginChild( "##folders_common", ImVec2( 0, ImGui::GetWindowHeight() / 3.0f ) );

            {
                {
                    ImGui::BeginChild( "##folders" );
                    {
                        DrawFolder( m_BaseProjectDir, true );
                    }
                    ImGui::EndChild();
                }
            }

            ImGui::EndChild();

            if ( ImGui::BeginDragDropTarget() )
            {
                auto data =
                     ImGui::AcceptDragDropPayload( "selectable", ImGuiDragDropFlags_AcceptNoDrawDefaultRect );
                if ( data )
                {
                    std::string* file = (std::string*)data->Data;
                    if ( MoveFile( *file, m_MovePath ) )
                    {
                        // LINFO("Moved File: %s to %s", file->c_str(), m_MovePath.c_str());
                    }
                    m_IsDragging = false;
                }
                ImGui::EndDragDropTarget();
            }
            float offset = 0.0f;
            if ( !vertical )
            {
                ImGui::NextColumn();
            }
            else
            {
                offset = ImGui::GetWindowHeight() / 3.0f + 6.0f;
                ImGui::Separator();
            }

            {
                {
                    ImGui::BeginChild(
                         "##directory_breadcrumbs",
                         ImVec2( ImGui::GetColumnWidth(), ImGui::GetFrameHeightWithSpacing() * 2.0f ) );

                    ImGui::AlignTextToFramePadding();
                    // Button for advanced settings
                    {
                        // Replace with actual style color
                        if ( ImGui::Button( ICON_MDI_COGS ) )
                            ImGui::OpenPopup( "SettingsPopup" );
                    }
                    if ( ImGui::BeginPopup( "SettingsPopup" ) )
                    {
                        if ( m_IsInListView )
                        {
                            if ( ImGui::Button( ICON_MDI_VIEW_LIST " Switch to Grid View" ) )
                            {
                                m_IsInListView = !m_IsInListView;
                            }
                        }
                        else
                        {
                            if ( ImGui::Button( ICON_MDI_VIEW_GRID " Switch to List View" ) )
                            {
                                m_IsInListView = !m_IsInListView;
                            }
                        }

                        if ( ImGui::Selectable( "Refresh" ) )
                        {
                            QueueRefresh();
                        }

                        if ( ImGui::Selectable( "New folder" ) )
                        {
                            std::string fullPath = m_CurrentDir->AssetPath + "/NewFolder";
                            std::filesystem::create_directory( fullPath );
                            QueueRefresh();
                        }

                        if ( !m_IsInListView )
                        {
                            ImGui::SliderFloat( "##GridSize", &m_GridSize, 40.0f, 400.0f );
                        }

                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();

                    ImGui::TextUnformatted( ICON_MDI_MAGNIFY );
                    ImGui::SameLine();

                    // Replace with actual filter implementation
                    // m_Filter.Draw("##Filter", ImGui::GetContentRegionAvail().x -
                    // ImGui::GetStyle().IndentSpacing);

                    if ( ImGui::Button( ICON_MDI_ARROW_LEFT ) )
                    {
                        if ( m_CurrentDir != m_BaseProjectDir )
                        {
                            ChangeDirectory( m_CurrentDir->Parent );
                        }
                    }
                    ImGui::SameLine();
                    if ( ImGui::Button( ICON_MDI_ARROW_RIGHT ) )
                    {
                        m_PreviousDirectory = m_CurrentDir;
                        // m_CurrentDir = m_LastNavPath;
                        m_UpdateNavigationPath = true;
                    }
                    ImGui::SameLine();

                    if ( m_UpdateNavigationPath )
                    {
                        m_BreadCrumbData.clear();
                        auto current = m_CurrentDir;
                        while ( current )
                        {
                            if ( current->Parent != nullptr )
                            {
                                m_BreadCrumbData.push_back( current );
                                current = current->Parent;
                            }
                            else
                            {
                                m_BreadCrumbData.push_back( m_BaseProjectDir );
                                current = nullptr;
                            }
                        }

                        for ( size_t i = 0; i < m_BreadCrumbData.size() / 2; i++ )
                        {
                            std::swap( m_BreadCrumbData[i], m_BreadCrumbData[m_BreadCrumbData.size() - i - 1] );
                        }

                        m_UpdateNavigationPath = false;
                    }
                    {
                        int newPwdLastSecIdx = -1;
                        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.1f, 0.2f, 0.7f, 0.0f ) );

                        for ( size_t i = 0; i < m_BreadCrumbData.size(); ++i )
                        {
                            auto*       directory = m_BreadCrumbData[i];
                            std::string fileName =
                                 std::filesystem::path( directory->AssetPath ).filename().string();

                            ImGui::PushID( directory );
                            if ( ImGui::SmallButton( fileName.c_str() ) )
                                ChangeDirectory( directory );
                            ImGui::PopID();
                            ImGui::SameLine();

                            if ( i + 1 < m_BreadCrumbData.size() )
                            {
                                ImGui::TextDisabled( ">" );
                                ImGui::SameLine();
                            }
                        }
                        ImGui::PopStyleColor();

                        if ( newPwdLastSecIdx >= 0 )
                        {
                            // Implementation for path navigation
                        }

                        ImGui::SameLine();
                    }
                    ImGui::EndChild();
                }

                {
                    int shownIndex = 0;

                    float xAvail = ImGui::GetContentRegionAvail().x;

                    constexpr float padding              = 4.0f;
                    const float     scaledThumbnailSize  = m_GridSize; // * ImGui::GetIO().FontGlobalScale;
                    const float     scaledThumbnailSizeX = scaledThumbnailSize * 0.55f;
                    const float     cellSize = scaledThumbnailSizeX + 2 * padding + scaledThumbnailSizeX * 0.1f;

                    constexpr float overlayPaddingY  = 6.0f * padding;
                    constexpr float thumbnailPadding = overlayPaddingY * 0.5f;
                    const float     thumbnailSize    = scaledThumbnailSizeX - thumbnailPadding;

                    const ImVec2 backgroundThumbnailSize = { scaledThumbnailSizeX + padding * 2,
                                                             scaledThumbnailSize + padding * 2 };

                    const float panelWidth  = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
                    int         columnCount = static_cast<int>( panelWidth / cellSize );
                    if ( columnCount < 1 )
                        columnCount = 1;

                    float lineHeight = ImGui::GetTextLineHeight();
                    int   flags      = ImGuiTableFlags_ContextMenuInBody | ImGuiTableFlags_ScrollY;

                    if ( m_IsInListView )
                    {
                        ImGui::PushStyleVar( ImGuiStyleVar_CellPadding, { 0, 0 } );
                        columnCount = 1;
                        flags |= ImGuiTableFlags_RowBg | ImGuiTableFlags_NoPadOuterX |
                                 ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_SizingStretchSame;
                    }
                    else
                    {
                        ImGui::PushStyleVar( ImGuiStyleVar_CellPadding,
                                             { scaledThumbnailSizeX * 0.05f, scaledThumbnailSizeX * 0.05f } );
                        flags |= ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingFixedFit;
                    }

                    ImVec2       cursorPos = ImGui::GetCursorPos();
                    const ImVec2 region    = ImGui::GetContentRegionAvail();
                    ImGui::InvisibleButton( "##DragDropTargetAssetPanelBody", region );

                    ImGui::SetCursorPos( cursorPos );

                    if ( ImGui::BeginTable( "BodyTable", columnCount, flags ) )
                    {
                        // Mouse scroll implementation would go here

                        m_GridItemsPerRow =
                             (int)floor( xAvail / ( m_GridSize + ImGui::GetStyle().ItemSpacing.x ) );
                        m_GridItemsPerRow = max( 1, m_GridItemsPerRow );

                        bool textureCreated = false;

                        // ImGuiUtilities::PushID();

                        if ( m_IsInListView )
                        {
                            for ( size_t i = 0; i < m_CurrentDir->Children.size(); i++ )
                            {
                                if ( !m_ShowHiddenFiles && m_CurrentDir->Children[i]->Hidden )
                                {
                                    continue;
                                }

                                // Filter implementation would go here

                                ImGui::TableNextColumn();
                                bool doubleClicked = RenderFile( (int)i, !m_CurrentDir->Children[i]->IsFile,
                                                                 shownIndex, !m_IsInListView );

                                if ( doubleClicked )
                                    break;
                                shownIndex++;
                            }
                        }
                        else
                        {
                            for ( size_t i = 0; i < m_CurrentDir->Children.size(); i++ )
                            {
                                if ( !m_ShowHiddenFiles && m_CurrentDir->Children[i]->Hidden )
                                {
                                    continue;
                                }

                                // Filter implementation would go here

                                ImGui::TableNextColumn();
                                bool doubleClicked = RenderFile( (int)i, !m_CurrentDir->Children[i]->IsFile,
                                                                 shownIndex, !m_IsInListView );

                                if ( doubleClicked )
                                    break;
                                shownIndex++;
                            }
                        }

                        // ImGuiUtilities::PopID();

                        if ( ImGui::BeginPopupContextWindow( "AssetPanelHierarchyContextWindow",
                                                             ImGuiPopupFlags_MouseButtonRight |
                                                                  ImGuiPopupFlags_NoOpenOverItems ) )
                        {
                            if ( std::filesystem::exists( m_CopiedPath ) && ImGui::Selectable( "Paste" ) )
                            {
                                // Paste implementation
                            }

                            if ( ImGui::Selectable( "Open Location" ) )
                            {
                                // Open location implementation
                            }

                            ImGui::Separator();

                            if ( ImGui::Selectable( "Import New Asset" ) )
                            {
                                // Import asset implementation
                            }

                            if ( ImGui::Selectable( "Refresh" ) )
                            {
                                Refresh();
                            }

                            if ( ImGui::Selectable( "New folder" ) )
                            {
                                std::string fullPath = m_CurrentDir->AssetPath + "/NewFolder";
                                std::filesystem::create_directory( fullPath );
                                Refresh();
                            }

                            if ( !m_IsInListView )
                            {
                                ImGui::SliderFloat( "##GridSize", &m_GridSize, m_MinGridSize, m_MaxGridSize );
                            }
                            ImGui::EndPopup();
                        }

                        ImGui::EndTable();
                    }
                    ImGui::PopStyleVar();
                }
            }

            if ( ImGui::BeginDragDropTarget() )
            {
                auto data =
                     ImGui::AcceptDragDropPayload( "selectable", ImGuiDragDropFlags_AcceptNoDrawDefaultRect );
                if ( data )
                {
                    std::string* a = (std::string*)data->Data;
                    if ( MoveFile( *a, m_MovePath ) )
                    {
                        // LINFO("Moved File: %s to %s", a->c_str(), m_MovePath.c_str());
                    }
                    m_IsDragging = false;
                }
                ImGui::EndDragDropTarget();
            }
        }
    }

    bool FileExplorerPanel::RenderFile( int dirIndex, bool folder, int shownIndex, bool gridView )
    {
        constexpr float padding              = 4.0f;
        const float     scaledThumbnailSize  = m_GridSize * ImGui::GetIO().FontGlobalScale;
        const float     scaledThumbnailSizeX = scaledThumbnailSize * 0.55f;
        const float     cellSize             = scaledThumbnailSizeX + 2 * padding + scaledThumbnailSizeX * 0.1f;

        constexpr float overlayPaddingY  = 6.0f * padding;
        constexpr float thumbnailPadding = overlayPaddingY * 0.5f;
        const float     thumbnailSize    = scaledThumbnailSizeX - thumbnailPadding;

        const ImVec2 backgroundThumbnailSize = { scaledThumbnailSizeX + padding * 2,
                                                 scaledThumbnailSize + padding * 2 };

        const float panelWidth  = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        int         columnCount = static_cast<int>( panelWidth / cellSize );
        if ( columnCount < 1 )
            columnCount = 1;

        bool doubleClicked = false;

        if ( gridView )
        {
            /* auto&                CurrentEnty = m_CurrentDir->Children[dirIndex];
             Graphics::Texture2D* textureId   = m_FolderIcon;

             auto cursorPos = ImGui::GetCursorPos();

             if ( CurrentEnty->IsFile )
             {
                 textureId = m_FileIcon;
                 switch ( CurrentEnty->Type )
                 {
                     case FileType::Texture:
                     {
                         if ( CurrentEnty->Thumbnail )
                         {
                             textureId = CurrentEnty->Thumbnail;
                         }
                         else if ( !textureCreated )
                         {
                             textureCreated = true;
                             if ( !m_Editor->GetAssetManager()->AssetExists( CurrentEnty->AssetPath ) )
                                 CurrentEnty->Thumbnail =
                                      m_Editor->GetAssetManager()->LoadTextureAsset( CurrentEnty->AssetPath, true
             ); else CurrentEnty->Thumbnail = m_Editor->GetAssetManager()
                                                               ->GetAssetData( CurrentEnty->AssetPath )
                                                               .As<Graphics::Texture2D>();
                             textureId = CurrentEnty->Thumbnail ? CurrentEnty->Thumbnail : m_FileIcon;
                         }
                         break;
                     }
                     case FileType::Scene:
                     {
                         ArenaTemp scratch = ArenaTempBegin( m_Arena );
                         String8   fileName =
                              PushStr8Copy( scratch.arena, StringUtilities::GetFileName( CurrentEnty->AssetPath )
             ); String8 sceneScreenShotPath = PushStr8F( scratch.arena, "%s/Scenes/Cache/%s.png", (const
             char*)m_BasePath.str, (const char*)fileName.str ); std::string sceneScreenShotPathtdString =
                              std::string( (const char*)sceneScreenShotPath.str, sceneScreenShotPath.size );
                         if ( std::filesystem::exists( std::filesystem::path( sceneScreenShotPathtdString ) ) )
                         {
                             textureCreated = true;
                             String8 sceneScreenShotAssetPath =
                                  PushStr8F( scratch.arena, "%s/Scenes/Cache/%s.png",
                                             (const char*)Str8Lit( "//Assets" ).str, (const char*)fileName.str );

                             if ( !m_Editor->GetAssetManager()->AssetExists( sceneScreenShotAssetPath ) )
                                 CurrentEnty->Thumbnail = m_Editor->GetAssetManager()->LoadTextureAsset(
                                      sceneScreenShotAssetPath, true );
                             else
                                 CurrentEnty->Thumbnail = m_Editor->GetAssetManager()
                                                               ->GetAssetData( sceneScreenShotAssetPath )
                                                               .As<Graphics::Texture2D>();
                             textureId = CurrentEnty->Thumbnail ? CurrentEnty->Thumbnail : m_FileIcon;
                         }
                         ArenaTempEnd( scratch );
                         break;
                     }
                     case FileType::Material:
                     case FileType::Model:
                     {
                         if ( CurrentEnty->Thumbnail )
                         {
                             textureId = CurrentEnty->Thumbnail;
                         }
                         else if ( !textureCreated )
                         {
                             textureCreated    = true;
                             ArenaTemp scratch = ArenaTempBegin( m_Arena );

                             String8 thumbnailPath;
                             String8 thumbnailAssetPath;
                             CreateThumbnailPath( scratch.arena, CurrentEnty, thumbnailAssetPath, thumbnailPath );

                             if ( std::filesystem::exists(
                                       std::filesystem::path( (const char*)thumbnailPath.str ) ) )
                             {
                                 textureCreated = true;
                                 if ( !m_Editor->GetAssetManager()->AssetExists( thumbnailAssetPath ) )
                                     CurrentEnty->Thumbnail =
                                          m_Editor->GetAssetManager()->LoadTextureAsset( thumbnailPath, true );
                                 else
                                     CurrentEnty->Thumbnail = m_Editor->GetAssetManager()
                                                                   ->GetAssetData( thumbnailAssetPath )
                                                                   .As<Graphics::Texture2D>();
                                 textureId = CurrentEnty->Thumbnail ? CurrentEnty->Thumbnail : m_FileIcon;
                             }
                             else
                             {
                                 m_Editor->RequestThumbnail( CurrentEnty->AssetPath );
                                 textureId = m_FileIcon;
                             }

                             ArenaTempEnd( scratch );
                         }
                         break;
                     }
                     default:
                         break;
                 }*/
        }
        bool flipImage = false;

        bool highlight = false;
        {
            highlight = m_CurrentDir->Children[dirIndex] == m_CurrentSelected;
        }

        // Background button
        bool const clicked = false; // ImGuiUtilities::ToggleButton(ImGuiUtilities::GenerateID(), highlight,
                                    // backgroundThumbnailSize,
                                    //  0.0f, 1.0f, ImGuiButtonFlags_AllowOverlap );
        if ( clicked )
        {
            m_CurrentSelected = m_CurrentDir->Children[dirIndex];
        }

        if ( ImGui::BeginPopupContextItem() )
        {
            m_CurrentSelected = m_CurrentDir->Children[dirIndex];

            if ( ImGui::Selectable( "Cut" ) )
            {
                m_CopiedPath = m_CurrentDir->Children[dirIndex]->AssetPath;
                m_CutFile    = true;
            }

            if ( ImGui::Selectable( "Copy" ) )
            {
                m_CopiedPath = m_CurrentDir->Children[dirIndex]->AssetPath;
                m_CutFile    = false;
            }

            if ( ImGui::Selectable( "Delete" ) )
            {
                /* ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                 String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                      temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ),
                      m_BasePath );
                 std::filesystem::remove_all( std::string( (const char*)fullPath.str, fullPath.size ) );
                 ScratchEnd( temp );*/
                QueueRefresh();
            }

            if ( ImGui::Selectable( "Duplicate" ) )
            {

                /* ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                 String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                      temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ),
                      m_BasePath );

                 std::filesystem::path fullPathFS = std::string( (const char*)fullPath.str, fullPath.size );
                 std::filesystem::path destinationPath = fullPathFS;

                 {
                     std::string filename  = fullPathFS.stem().string();
                     std::string extension = fullPathFS.extension().string();

                     while ( std::filesystem::exists( destinationPath ) )
                     {
                         filename += "_copy";
                         destinationPath = destinationPath.parent_path() / ( filename + extension );
                     }
                 }
                 std::filesystem::copy( fullPathFS, destinationPath );

                 ScratchEnd( temp );*/
                QueueRefresh();
            }

            ImGui::Separator();

            if ( ImGui::Selectable( "Open Location" ) )
            {
                /*ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                     temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ),
                     m_BasePath );
                Lumos::OS::Get().OpenFileLocation( std::string( (const char*)fullPath.str, fullPath.size ) );
                ScratchEnd( temp );*/
            }

            if ( m_CurrentDir->Children[dirIndex]->IsFile && ImGui::Selectable( "Open External" ) )
            {
                /*ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                     temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ),
                     m_BasePath );
                Lumos::OS::Get().OpenFileExternal( std::string( (const char*)fullPath.str, fullPath.size ) );
                ScratchEnd( temp );*/
            }

            if ( ImGui::Selectable( "Copy Full Path" ) )
            {
                /*  ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                  String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                       temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ),
                       m_BasePath );
                  ImGui::SetClipboardText( (const char*)ToStdString( fullPath ).c_str() );
                  ScratchEnd( temp );*/
            }

            if ( m_CurrentDir->Children[dirIndex]->IsFile && ImGui::Selectable( "Copy Asset Path" ) )
            {
                /*  ImGui::SetClipboardText(
                       (const char*)ToStdString( m_CurrentDir->Children[dirIndex]->AssetPath ).c_str() );*/
            }

            ImGui::Separator();

            if ( ImGui::Selectable( "Import New Asset" ) )
            {
                //   m_Editor->OpenFile();
            }

            if ( ImGui::Selectable( "Refresh" ) )
            {
                QueueRefresh();
            }

            if ( ImGui::Selectable( "New folder" ) )
            {
                /*ArenaTemp temp     = ScratchBegin( &m_Arena, 1 );
                String8   fullPath = StringUtilities::RelativeToAbsolutePath(
                     temp.arena, m_CurrentDir->AssetPath, Str8Lit( "//Assets" ), m_BasePath );
                std::filesystem::create_directory( std::filesystem::path(
                     std::string( (const char*)fullPath.str, fullPath.size ) + "/NewFolder" ) );
                ScratchEnd( temp );*/

                QueueRefresh();
            }

            if ( !m_IsInListView )
            {
                // ImGui::SliderFloat( "##GridSize", &m_GridSize, MinGridSize, MaxGridSize );
            }
            ImGui::EndPopup();
        }

        if ( ImGui::IsItemHovered() /*&& m_CurrentDir->Children[dirIndex]->Thumbnail*/ )
        {
            /* Vec2 TooltipSize =
                  GetAspectCorrectedSize( Vec2( textureId->GetWidth(), textureId->GetHeight() ), 512 );
             ImGuiUtilities::Tooltip( m_CurrentDir->Children[dirIndex]->Thumbnail, TooltipSize,
                                      (const char*)( m_CurrentDir->Children[dirIndex]->AssetPath.str ) );*/
        }
        else
        {

            //  ImGuiUtilities::Tooltip( (const char*)( m_CurrentDir->Children[dirIndex]->AssetPath.str ) );
        }

        if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceAllowNullID ) )
        {
            const std::string& assetPath = m_CurrentDir->Children[dirIndex]->AssetPath;
            const FileType     fileType  = m_CurrentDir->Children[dirIndex]->Type;

            // Generic payload for all asset files
            ImGui::SetDragDropPayload( "AssetFile", assetPath.c_str(), assetPath.size() + 1 );

            // Prefab-specific payload so the hierarchy panel can instantiate directly
            if ( fileType == FileType::Prefab )
                ImGui::SetDragDropPayload( "PREFAB_FILE", assetPath.c_str(), assetPath.size() + 1 );

            ImGui::TextUnformatted( assetPath.c_str() );
            m_IsDragging = true;
            ImGui::EndDragDropSource();
        }

        if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
        {
            doubleClicked = true;
        }

        /* ImGui::SetCursorPos( { cursorPos.x + padding, cursorPos.y + padding } );
         ImGui::SetNextItemAllowOverlap();
         ImGui::Image(
              reinterpret_cast<ImTextureID>( Application::Get().GetImGuiManager()->GetImGuiRenderer()->AddTexture(
                   Graphics::Material::GetDefaultTexture() ) ),
              { backgroundThumbnailSize.x - padding * 2.0f, backgroundThumbnailSize.y - padding * 2.0f }, { 0, 0 },
              { 1, 1 }, ImGui::GetStyleColorVec4( ImGuiCol_WindowBg ) + ImVec4( 0.04f, 0.04f, 0.04f, 0.04f ) );

         ImGui::SetCursorPos( { cursorPos.x + thumbnailPadding * 0.75f, cursorPos.y + thumbnailPadding } );
         ImGui::SetNextItemAllowOverlap();

         Vec2 correctedSize =
              GetAspectCorrectedSize( Vec2( textureId->GetWidth(), textureId->GetHeight() ), thumbnailSize );
         Vec2 padding2 = ( Vec2( thumbnailSize ) - correctedSize ) * 0.5f;
         ImGui::SetCursorPos( ImGui::GetCursorPos() + ImVec2( padding2.x, padding2.y ) );
         ImGuiUtilities::Image( textureId, correctedSize );

         const ImVec2 typeColorFrameSize = { scaledThumbnailSizeX, scaledThumbnailSizeX * 0.03f };
         ImGui::SetCursorPosX( cursorPos.x + padding );
         ImGui::Image(
              reinterpret_cast<ImTextureID>( Application::Get().GetImGuiManager()->GetImGuiRenderer()->AddTexture(
                   Graphics::Material::GetDefaultTexture() ) ),
              typeColorFrameSize, ImVec2( 0.0f, flipImage ? 1.0f : 0.0f ), ImVec2( 1.0f, flipImage ? 0.0f : 1.0f ),
              !CurrentEnty->IsFile ? ImVec4( 0.0f, 0.0f, 0.0f, 0.0f ) : CurrentEnty->FileTypeColour );

         const ImVec2 rectMin  = ImGui::GetItemRectMin() + ImVec2( 0.0f, 8.0f );
         const ImVec2 rectSize = ImGui::GetItemRectSize() + ImVec2( 0.0f, 4.0f );
         const ImRect clipRect =
              ImRect( { rectMin.x + padding * 2.0f, rectMin.y + padding * 4.0f },
                      { rectMin.x + rectSize.x, rectMin.y + scaledThumbnailSizeX -
                                                     ImGui::GetIO().Fonts->Fonts[2]->FontSize - padding * 4.0f }
         );*/

        {
            /* if ( m_GridSize < 140 * (float)m_Editor->GetWindow()->GetDPIScale() )
             {
                 ImGuiUtilities::ScopedFont smallFont( ImGui::GetIO().Fonts->Fonts[2] );
                 ImGuiUtilities::ClippedText(
                      clipRect.Min, clipRect.Max,
                      (const char*)( StringUtilities::GetFileName( CurrentEnty->AssetPath, !CurrentEnty->IsFile )
                                          .str ),
                      nullptr, nullptr, { 0, 0 }, nullptr, clipRect.GetSize().x );
             }
             else
             {
                 ImGuiUtilities::ClippedText(
                      clipRect.Min, clipRect.Max,
                      (const char*)( StringUtilities::GetFileName( CurrentEnty->AssetPath, !CurrentEnty->IsFile )
                                          .str ),
                      nullptr, nullptr, { 0, 0 }, nullptr, clipRect.GetSize().x );
             }*/
        }

        // if ( CurrentEnty->IsFile )
        //{
        //     ImGui::SetCursorPos( { cursorPos.x + padding * (float)m_Editor->GetWindow()->GetDPIScale(),
        //                            cursorPos.y + backgroundThumbnailSize.y -
        //                                 ( ImGui::GetIO().Fonts->Fonts[2]->FontSize -
        //                                   padding * (float)m_Editor->GetWindow()->GetDPIScale() ) *
        //                                      3.3f } );
        //     ImGui::BeginDisabled();
        //     ImGuiUtilities::ScopedFont smallFont( ImGui::GetIO().Fonts->Fonts[2] );
        //     ImGui::Indent();

        //    String8     fileTypeString   = s_FileTypesToString.at( FileType::Unknown );
        //    const auto& fileStringTypeIt = s_FileTypesToString.find( CurrentEnty->Type );
        //    if ( fileStringTypeIt != s_FileTypesToString.end() )
        //        fileTypeString = fileStringTypeIt->second;

        //    ImGui::TextUnformatted( (const char*)( fileTypeString ).str );
        //    ImGui::Unindent();
        //    cursorPos = ImGui::GetCursorPos();
        //    ImGui::SetCursorPos( { cursorPos.x + padding * (float)m_Editor->GetWindow()->GetDPIScale(),
        //                           cursorPos.y - ( ImGui::GetIO().Fonts->Fonts[2]->FontSize * 0.8f -
        //                                           padding * (float)m_Editor->GetWindow()->GetDPIScale() ) } );
        //    ImGui::Indent();

        //    ImGui::TextUnformatted( StringUtilities::BytesToString( CurrentEnty->FileSize ).c_str() );
        //    ImGui::Unindent();

        //    ImGui::EndDisabled();
        //}

        // if ( CurrentEnty->IsFile )
        //{
        //     ImGui::SetCursorPos( { cursorPos.x + padding * (float)m_Editor->GetWindow()->GetDPIScale(),
        //                            cursorPos.y + backgroundThumbnailSize.y -
        //                                 ( ImGui::GetIO().Fonts->Fonts[2]->FontSize -
        //                                   padding * (float)m_Editor->GetWindow()->GetDPIScale() ) *
        //                                      3.3f } );
        //     ImGui::BeginDisabled();
        //     ImGuiUtilities::ScopedFont smallFont( ImGui::GetIO().Fonts->Fonts[2] );
        //     ImGui::Indent();

        //    String8     fileTypeString   = s_FileTypesToString.at( FileType::Unknown );
        //    const auto& fileStringTypeIt = s_FileTypesToString.find( CurrentEnty->Type );
        //    if ( fileStringTypeIt != s_FileTypesToString.end() )
        //        fileTypeString = fileStringTypeIt->second;

        //    ImGui::TextUnformatted( (const char*)( fileTypeString ).str );
        //    ImGui::Unindent();
        //    cursorPos = ImGui::GetCursorPos();
        //    ImGui::SetCursorPos( { cursorPos.x + padding * (float)m_Editor->GetWindow()->GetDPIScale(),
        //                           cursorPos.y - ( ImGui::GetIO().Fonts->Fonts[2]->FontSize * 0.8f -
        //                                           padding * (float)m_Editor->GetWindow()->GetDPIScale() ) } );
        //    ImGui::Indent();

        //    ImGui::TextUnformatted( StringUtilities::BytesToString( CurrentEnty->FileSize ).c_str() );
        //    ImGui::Unindent();

        //    ImGui::EndDisabled();
        //}
       // else
        {
          /*  ImGui::TextUnformatted( folder ? ICON_MDI_FOLDER
                                           : m_Editor->GetIconFontIcon( std::string(
                                                  (const char*)m_CurrentDir->Children[dirIndex]->AssetPath.str,
                                                  m_CurrentDir->Children[dirIndex]->AssetPath.size ) ) );
            ImGui::SameLine();

            if ( ImGui::Selectable(
                      (const char*)StringUtilities::GetFileName( m_CurrentDir->Children[dirIndex]->AssetPath,
                                                                 !m_CurrentDir->Children[dirIndex]->IsFile )
                           .str,
                      false, ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                {
                    doubleClicked = true;
                }
            }

            ImGuiUtilities::Tooltip( (const char*)m_CurrentDir->Children[dirIndex]->AssetPath.str );

            if ( ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceAllowNullID ) )
            {
                ImGui::TextUnformatted(
                     m_Editor->GetIconFontIcon( ToStdString( m_CurrentDir->Children[dirIndex]->AssetPath ) ) );

                ImGui::SameLine();
                m_MovePath = m_CurrentDir->Children[dirIndex]->AssetPath;
                ImGui::TextUnformatted( (const char*)ToStdString( m_MovePath ).c_str() );

                size_t size = sizeof( const char* ) + m_MovePath.size;
                ImGui::SetDragDropPayload( "AssetFile", m_MovePath.str, size );
                m_IsDragging = true;
                ImGui::EndDragDropSource();
            }*/
        }

        if ( doubleClicked )
        {
           /* if ( folder )
            {
                ChangeDirectory( m_CurrentDir->Children[dirIndex] );
            }
            else
            {
                ArenaTemp temp        = ScratchBegin( &m_Arena, 1 );
                String8   currentPath = StringUtilities::RelativeToAbsolutePath(
                     temp.arena, m_CurrentDir->Children[dirIndex]->AssetPath, Str8Lit( "//Assets" ), m_BasePath );
                m_Editor->FileOpenCallback( std::string( (const char*)currentPath.str, currentPath.size ) );
                ScratchEnd( temp );
            }*/
        }

        return doubleClicked;
    }

    void FileExplorerPanel::Refresh()
    {
        std::string currentPath = m_CurrentDir->AssetPath;

        m_Directories.clear();

        m_BasePath                      = "Assets"; // Replace with actual path
        std::string baseDirectoryHandle = ProcessDirectory( m_BasePath, nullptr, true );
        m_BaseProjectDir                = m_Directories[baseDirectoryHandle].get();
        ChangeDirectory( m_BaseProjectDir );

        m_UpdateNavigationPath = true;

        m_BaseProjectDir    = m_Directories[baseDirectoryHandle].get();
        m_PreviousDirectory = nullptr;
        m_CurrentDir        = nullptr;

        bool dirFound = false;
        for ( auto& dir : m_Directories )
        {
            if ( dir.first == currentPath )
            {
                m_CurrentDir = dir.second.get();
                dirFound     = true;
                break;
            }
        }
        if ( !dirFound )
            ChangeDirectory( m_BaseProjectDir );
        else
            ChangeDirectory( m_CurrentDir );
    }

    void FileExplorerPanel::CreateThumbnailPath( DirectoryInformation* directoryInfo, std::string& assetPath,
                                                 std::string& AbsolutePath )
    {
        std::string assetPath1    = directoryInfo->AssetPath;
        std::string thumbnailPath = assetPath1 + "_thumbnail.png";

        assetPath    = thumbnailPath; // Simplified path conversion
        AbsolutePath = thumbnailPath; // Simplified path conversion
    }

} // namespace Desert::Editor