// Desert Engine Launcher — standalone project browser, fully separate from the Editor: it links NO
// engine code (R1) — only GLFW + ImGui + stb_image + ReflectCpp, and compiles the shared serializer
// from the desert-shared submodule itself.
//
//   * a GRID of projects read from ~/.desertengine/projects.json, each resolved against the disk so
//     a project that is gone or corrupt says so on its tile, with a picture, a name and a relative
//     time; search, selection, double-click and a context menu on top of it
//   * New Project, from templates SCANNED off the engine install (Templates/<Id>/template.json) —
//     adding a template is dropping a folder in, not rebuilding this binary
//   * Project Settings — the three fields a project actually has, and a written refusal in place of
//     the settings framework nothing has a consumer for
//   * "Open" launches the Editor (Debug/Release picked in the sidebar) through an argv spawn
//
// This file is the WINDOW. Everything it does that could be wrong without a window on screen lives
// next door and is covered by Tests/Tools/ProjectHubContracts: Projects.hpp (registry entries,
// relative time, the column rule, name validation, the template scan, project creation),
// HubConfig.hpp (where the registries are and which engine is chosen), Thumbnails.hpp (the decode
// budget), Launch.hpp (the argv the Editor is started with), Files.hpp (the two file primitives),
// FileDialog.hpp (native panels).
//
// STILL OPENGL2, and deliberately: R2 makes Vulkan the only supported backend, but porting the
// window is L3 — it needs the shared VulkanWindow and the MoltenVK ICD environment, and doing it
// here would collide with the repository move. The port is contained ON PURPOSE: the four GL calls
// at the bottom of the frame loop, and the twenty lines of `MakeTextureBackend` below. Nothing else
// in the launcher — not the thumbnail cache, not a screen — names a graphics API.

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <GLFW/glfw3.h>

#include <DesertShared/LaunchProtocol.hpp>
#include <DesertShared/ProjectFormat.hpp>

#include "FileDialog.hpp"
#include "Files.hpp"
#include "HubConfig.hpp"
#include "Launch.hpp"
#include "Projects.hpp"
#include "Theme.hpp"
#include "Thumbnails.hpp"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined( _WIN32 )
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Material Design icon literals (byte-identical to the editor's IconsMaterialDesignIcons.hpp).
#define ICON_PLUS "\xf3\xb0\x90\x95"
#define ICON_FOLDER_OPEN "\xf3\xb0\xb7\x8f"
#define ICON_TRASH "\xf3\xb0\xa9\xba"
#define ICON_ROCKET "\xf3\xb1\x93\x9f"
#define ICON_ALERT_CIRCLE "\xf3\xb0\x97\x96"
#define ICON_ALERT "\xf3\xb0\x80\xaa"
#define ICON_CHEVRON_LEFT "\xf3\xb0\x85\x81"
#define ICON_CHEVRON_RIGHT "\xf3\xb0\x85\x82"
#define ICON_MAGNIFY "\xf3\xb0\x8d\x89"
#define ICON_CLOCK "\xf3\xb0\x85\x90"
#define ICON_IMAGE_OFF "\xf3\xb1\x87\x91"
#define ICON_DOTS "\xf3\xb0\x87\x98"
#define ICON_VIEW_GRID "\xf3\xb1\x87\x99"
#define ICON_SAVE "\xf3\xb0\xa0\x98"
#define ICON_REFRESH "\xf3\xb0\x91\x90"
#define ICON_LOCK "\xf3\xb0\x8d\x81"
#define ICON_INFO "\xf3\xb0\x8b\xbd"
#define ICON_OPEN_IN_NEW "\xf3\xb0\x8f\x8c"
#define ICON_CHECK "\xf3\xb0\x97\xa0"
#define ICON_COPY "\xf3\xb0\x86\x8f"
#define ICON_COG "\xf3\xb0\xa2\xbb"
#define ICON_PACKAGE "\xf3\xb0\x8f\x97"

namespace
{
    namespace fs = std::filesystem;
    using namespace Hub::Theme;

    // The LAUNCHER's own version. The ENGINE's version is a property of the engine install and is
    // read out of engines.json — the launcher links no Common::Version and never could (R1).
    constexpr const char* kHubVersion = "0.2.0";

    // Layout constants that more than one screen depends on. L2 §6.1.
    constexpr float kSidebarWidth = 230.0f;
    constexpr float kPad          = 24.0f;
    // RESERVED, always, and sized for TWO wrapped lines. The messages this strip carries are errno
    // text and paths, and one line is not what those measure. Before this the status line lived at
    // the END of the scrolling project list, so the one moment it mattered most — a launch that
    // failed with fourteen projects on screen — was the moment it sat below the fold, unread.
    constexpr float kStatusHeight = 46.0f;
    constexpr float kTileGap      = 16.0f;
    // A two-column window must not grow poster-sized tiles. Between column steps the tile STRETCHES
    // rather than leaving a gutter, so the cap is what stops the stretch from becoming absurd.
    constexpr float kTileMaxWidth = 340.0f;

    enum class View
    {
        Projects,
        NewProject,
        Settings
    };

    struct HubState;
    // Loads the settings screen's editable copy of a project. Declared here because the projects
    // screen opens it and is written first; the body is down with the rest of that screen.
    void EnterSettings( HubState& st, int index );

    struct HubState
    {
        std::string ConfigDirectory = Hub::ConfigDirectory();

        Common::Project::ProjectsRegistry Registry;
        std::vector<Hub::ProjectEntry>    Entries;
        double                            EntriesResolvedAt = -1.0;

        Hub::EngineChoice   Engine;
        Hub::TemplateScan   TemplateScan;
        Hub::ThumbnailCache Thumbnails;

        View        Screen = View::Projects;
        int         Config = 1; // 0 = Debug, 1 = Release
        GLFWwindow* Window = nullptr;

        // ── Projects screen ──
        char  Search[128] = "";
        int   Selected    = -1; // index into Entries
        float Scroll      = 0.0f;
        int   MenuIndex   = -1; // the tile whose context menu is open

        // ── New Project screen ──
        char NewName[128]     = "MyGame";
        char NewLocation[512] = "";
        int  Template         = 0; // index into TemplateScan.Templates

        // ── Settings screen ──
        int         SettingsIndex        = -1; // index into Entries
        char        EditName[128]        = "";
        char        EditDescription[512] = "";
        std::string EditDefaultScene;
        // The descriptor as it is ON DISK, read when the screen is entered. Re-reading it every
        // frame would be a file read per frame for three lines that cannot change while the screen
        // is up without this launcher being the one to change them.
        Common::Project::ProjectFile Descriptor;
        std::vector<std::string>     Scenes;     // Assets/Scenes/**/*.desce, project-relative
        std::string                  SizeOnDisk; // "" until measured
        double                       SizeMeasuredAt = -1.0;
        bool                         SettingsDirty  = false;

        // ── the status strip ──
        std::string Status;
        bool        StatusIsError = false;

        // Re-resolves every registry line against the disk. Called on every mutation, and at most
        // once a second while the window is up: a project can be deleted, restored or repaired by
        // some other program while the launcher sits open, and a tile claiming otherwise is the
        // defect this whole state exists to close.
        void ResolveEntries()
        {
            Entries           = Hub::ResolveProjectEntries( Registry );
            EntriesResolvedAt = ImGui::GetTime();
        }

        void ResolveEntriesIfStale()
        {
            if ( ImGui::GetTime() - EntriesResolvedAt >= 1.0 )
                ResolveEntries();
        }

        void SetError( const std::string& message )
        {
            Status        = message;
            StatusIsError = true;
        }

        void SetNotice( const std::string& message )
        {
            Status        = message;
            StatusIsError = false;
        }
    };

    // ────────────────────────────────────────────────────────────── widgets over ImGui

    enum class BtnKind
    {
        Accent, // the one primary action on a screen
        Solid,
        Ghost,
        Danger,
        Disabled
    };

    bool Button( ImVec2 at, const char* label, BtnKind kind, float width = 0.0f, float height = 32.0f )
    {
        ImU32 fill = kPanelAlt, border = kBorder, text = kText;
        switch ( kind )
        {
            case BtnKind::Accent:
                fill   = kAccent;
                border = 0;
                text   = kOnAccent;
                break;
            case BtnKind::Solid:
                break;
            case BtnKind::Ghost:
                fill = 0;
                text = kTextDim;
                break;
            case BtnKind::Danger:
                fill   = Fade( kError, 0.14f );
                border = Fade( kError, 0.55f );
                text   = kError;
                break;
            case BtnKind::Disabled:
                fill   = Mix( kPanel, kInk, 0.4f );
                border = kBorderSoft;
                text   = kTextFaint;
                break;
        }
        if ( width <= 0.0f )
            width = TextW( label, Regular ) + 26.0f;

        ImGui::SetCursorScreenPos( at );
        ImGui::PushStyleColor( ImGuiCol_Button, V4( fill ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered,
                               V4( kind == BtnKind::Accent ? kAccentHi : Mix( fill, kText, 0.14f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, V4( Mix( fill, kInk, 0.25f ) ) );
        ImGui::PushStyleColor( ImGuiCol_Border, V4( border ) );
        ImGui::PushStyleColor( ImGuiCol_Text, V4( text ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, border ? 1.0f : 0.0f );
        const bool pressed = ImGui::Button( label, ImVec2( width, height ) ) && kind != BtnKind::Disabled;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 5 );
        return pressed;
    }

    // A label + a sunken field, laid out on a rect rather than in the ImGui cursor flow, so a screen
    // can be described by its geometry the way the design is.
    bool InputField( Rect r, const char* id, char* buffer, size_t size, const char* hint = nullptr )
    {
        ImGui::SetCursorScreenPos( r.Min() );
        ImGui::SetNextItemWidth( r.W() );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f, ( r.H() - Regular->FontSize ) * 0.5f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, V4( kBorderSoft ) );
        const bool changed =
             hint ? ImGui::InputTextWithHint( id, hint, buffer, size ) : ImGui::InputText( id, buffer, size );
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        return changed;
    }

    void SectionLabel( ImVec2 at, const char* text )
    {
        TextAt( at, kTextFaint, text, Tiny );
    }

    // ────────────────────────────────────────────────────────────── the launcher frame

    struct Frame
    {
        Rect Window;
        Rect Content;
        Rect Status;
    };

    void DrawWordmark( ImVec2 at )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // A mark, not a logo: three stacked dune ridges in the sand, then the name. Deliberately
        // trivial to replace when brand work happens.
        for ( int i = 0; i < 3; ++i )
        {
            const float y = at.y + 6.0f + static_cast<float>( i ) * 7.0f;
            const float w = 22.0f - static_cast<float>( i ) * 5.0f;
            dl->AddLine( ImVec2( at.x, y ), ImVec2( at.x + w, y ),
                         Fade( kAccent, 1.0f - static_cast<float>( i ) * 0.28f ), 3.0f );
        }
        TextAt( ImVec2( at.x + 32, at.y - 3 ), kText, "DesertEngine", Title );
        TextAt( ImVec2( at.x + 33, at.y + 20 ), kTextFaint, "LAUNCHER", Tiny );
    }

    Frame DrawFrame( HubState& st, float width, float height )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const Rect  window{ 0.0f, 0.0f, width, height };
        Box( window, kCanvas );

        const Rect side{ 0.0f, 0.0f, kSidebarWidth, height };
        Box( side, kInk );
        dl->AddLine( ImVec2( side.x1, side.y0 ), ImVec2( side.x1, side.y1 ), IM_COL32( 0, 0, 0, 255 ) );

        DrawWordmark( ImVec2( side.x0 + 22, side.y0 + 26 ) );

        // ONE rail item. The design's sidebar has a second — Collections — and it is deliberately
        // absent: collections are stage 2 and nothing is behind that click today, and a nav item
        // that leads nowhere is exactly the dead control the contract forbids. It arrives with the
        // screens behind it, not before them.
        {
            const Rect r{ side.x0 + 12, side.y0 + 84, side.x1 - 12, side.y0 + 122 };
            Box( r, Fade( kAccent, 0.13f ), 0, 5.0f );
            dl->AddRectFilled( ImVec2( r.x0, r.y0 + 8 ), ImVec2( r.x0 + 3, r.y1 - 8 ), kAccent, 2.0f );
            TextAt( ImVec2( r.x0 + 16, r.y0 + 10 ), kAccent, ICON_VIEW_GRID );
            TextAt( ImVec2( r.x0 + 42, r.y0 + 10 ), kText, "Projects" );
        }

        // Bottom block: which engine was found, and which build the Editor is launched as.
        const float by = height - 118.0f;
        dl->AddLine( ImVec2( side.x0 + 20, by - 12 ), ImVec2( side.x1 - 20, by - 12 ),
                     IM_COL32( 0x1E, 0x1E, 0x24, 255 ) );
        TextAt( ImVec2( side.x0 + 22, by ), kTextFaint, "ENGINE", Tiny );
        if ( st.Engine.Root.empty() )
        {
            // No engine is a STATE, and the sidebar is where it is said. The reason travels with it,
            // because "not found" without "and here is where I looked" is not actionable.
            TextAt( ImVec2( side.x0 + 22, by + 15 ), kError, "none found", Bold );
        }
        else
        {
            // "0.1.492+768c3f1" is the full version; the tile of the sidebar wants the number.
            std::string shown = st.Engine.VersionFull;
            if ( const size_t plus = shown.find( '+' ); plus != std::string::npos )
                shown.erase( plus );
            // An engine found through DESERT_ROOT carries no version — nothing has registered it.
            // Dim, and named as what it is, rather than bold text asserting a fact nobody supplied.
            const bool unregistered = shown.empty();
            if ( unregistered )
                shown = "version not registered";
            TextAt( ImVec2( side.x0 + 22, by + 15 ), unregistered ? kTextDim : kText, shown.c_str(), Bold );
            if ( st.Engine.CommitCount > 0 )
            {
                char build[32];
                std::snprintf( build, sizeof( build ), "build %d", st.Engine.CommitCount );
                TextAt( ImVec2( side.x0 + 22 + TextW( shown.c_str(), Bold ) + 8, by + 17 ), kTextFaint, build,
                        Tiny );
            }
        }
        if ( ImGui::IsMouseHoveringRect( ImVec2( side.x0 + 20, by - 4 ), ImVec2( side.x1 - 20, by + 32 ) ) )
            ImGui::SetTooltip( "%s\n%s", st.Engine.Root.empty() ? "(no engine)" : st.Engine.Root.c_str(),
                               st.Engine.Explanation.c_str() );

        {
            ImGui::SetCursorScreenPos( ImVec2( side.x0 + 22, by + 38 ) );
            ImGui::SetNextItemWidth( side.W() - 44.0f );
            // The labels ARE the config names the run scripts receive — one spelling, the protocol's.
            const char* configs[] = { Common::Launch::kConfigDebug, Common::Launch::kConfigRelease };
            ImGui::PushStyleColor( ImGuiCol_FrameBg, V4( kPanelAlt ) );
            ImGui::PushStyleColor( ImGuiCol_Border, V4( kBorder ) );
            ImGui::Combo( "##config", &st.Config, configs, 2 );
            ImGui::PopStyleColor( 2 );
        }
        {
            char line[64];
            std::snprintf( line, sizeof( line ), "Launcher %s", kHubVersion );
            TextAt( ImVec2( side.x0 + 22, by + 76 ), kTextFaint, line, Tiny );
        }

        Frame f;
        f.Window  = window;
        f.Content = Rect{ side.x1 + kPad, kPad, window.x1 - kPad, window.y1 - kPad - kStatusHeight - 8.0f };
        f.Status  = Rect{ side.x1 + kPad, window.y1 - kPad - kStatusHeight, window.x1 - kPad, window.y1 - kPad };
        return f;
    }

    void DrawStatusStrip( HubState& st, Rect strip )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if ( st.Status.empty() )
        {
            // The space is reserved either way — that is the whole point. A strip that appears makes
            // everything above it jump at the exact moment the user is trying to read something.
            dl->AddLine( ImVec2( strip.x0, strip.y0 ), ImVec2( strip.x1, strip.y0 ),
                         IM_COL32( 0x20, 0x20, 0x24, 255 ) );
            return;
        }

        const ImU32 col = st.StatusIsError ? kError : kInfo;
        Box( strip, Fade( col, 0.10f ), Fade( col, 0.45f ), 4.0f );
        TextAt( ImVec2( strip.x0 + 10, strip.y0 + 7 ), col, ICON_ALERT_CIRCLE );

        // A copy button, because these strings are paths, errno text and parser messages that people
        // paste into an issue.
        const float copyWidth = TextW( "Copy", Small ) + 22.0f;
        if ( Button( ImVec2( strip.x1 - 8 - copyWidth, strip.y0 + 9 ), "Copy", BtnKind::Danger, copyWidth,
                     26.0f ) )
            ImGui::SetClipboardText( st.Status.c_str() );

        Wrapped( ImVec2( strip.x0 + 32, strip.y0 + 6 ), col, st.Status.c_str(), strip.W() - 46.0f - copyWidth,
                 Small );
    }

    // ────────────────────────────────────────────────────────────── actions

    void PersistRegistry( HubState& st )
    {
        if ( const auto saved = Hub::SaveProjects( st.ConfigDirectory, st.Registry ); !saved.IsSuccess() )
            st.SetError( "Could not update " + Hub::ProjectsRegistryFile( st.ConfigDirectory ) + ": " +
                         saved.GetError() );
        st.ResolveEntries();
    }

    void OpenProject( HubState& st, const Hub::ProjectEntry& entry )
    {
        // The entry already knows why it cannot open; re-deriving it here would be a second answer
        // to one question.
        if ( !entry.IsOpenable() )
        {
            st.SetError( entry.Path + " - " + entry.Trouble );
            return;
        }
        if ( st.Engine.Root.empty() )
        {
            st.SetError( "Cannot start the Editor: " + st.Engine.Explanation );
            return;
        }

        const std::string config = st.Config == 0 ? Common::Launch::kConfigDebug : Common::Launch::kConfigRelease;
        const auto launched = Hub::SpawnDetached( Hub::BuildEditorLaunch( st.Engine.Root, config, entry.Path ) );
        if ( !launched.IsSuccess() )
        {
            // The old code discarded std::system()'s result and returned true no matter what, so a
            // launch that never happened still closed the window.
            st.SetError( "Could not start the Editor: " + launched.GetError() +
                         ". The project was not opened and the recent list was not changed." );
            return;
        }

        Common::Project::PromoteRecent( st.Registry, entry.Path, Common::Project::UnixNow() );
        if ( const auto saved = Hub::SaveProjects( st.ConfigDirectory, st.Registry ); !saved.IsSuccess() )
        {
            // The Editor IS starting, so this must not read as a failed open — but the launcher
            // stays up instead of closing over an error nobody would ever see.
            st.SetError( "The Editor is starting, but " + Hub::ProjectsRegistryFile( st.ConfigDirectory ) +
                         " could not be updated: " + saved.GetError() );
            st.ResolveEntries();
            return;
        }
        glfwSetWindowShouldClose( st.Window, GLFW_TRUE ); // the launcher's job is done
    }

    void RevealProject( HubState& st, const Hub::ProjectEntry& entry )
    {
        if ( const auto revealed = Hub::SpawnDetached( Hub::BuildRevealCommand( entry.Path ) );
             !revealed.IsSuccess() )
            st.SetError( "Could not reveal " + entry.Path + ": " + revealed.GetError() );
    }

    void RemoveEntry( HubState& st, int index )
    {
        if ( index < 0 || index >= static_cast<int>( st.Registry.Projects.size() ) )
            return;
        st.Registry.Projects.erase( st.Registry.Projects.begin() + index );
        if ( st.Selected == index )
            st.Selected = -1;
        else if ( st.Selected > index )
            --st.Selected;
        PersistRegistry( st );
    }

    void OpenExisting( HubState& st )
    {
        const std::string chosen =
             Hub::FileDialog::OpenFile( "Open existing project", "Desert project (.deproj)", "deproj" );
        if ( chosen.empty() ) // cancelled
            return;
        OpenProject( st, Hub::ResolveProjectEntry( chosen ) );
    }

    // ────────────────────────────────────────────────────────────── the project tile

    enum class TileState
    {
        Normal,
        Hover,
        Selected,
        Missing
    };

    // What a click on a hovered tile landed on. The hover band puts two controls on the picture, and
    // the tile underneath is still a click target — so the answer has to be one value, not three
    // overlapping booleans.
    enum class TileHit
    {
        Body,
        Open,
        Menu
    };

    TileHit DrawProjectTile( HubState& st, Rect r, const Hub::ProjectEntry& entry, TileState state, long long now )
    {
        ImDrawList*     dl       = ImGui::GetWindowDrawList();
        const bool      missing  = !entry.IsOpenable();
        constexpr float rounding = 6.0f;

        ImU32 fill = kPanel, border = kBorderSoft;
        if ( state == TileState::Hover )
            fill = kPanelAlt;
        if ( state == TileState::Selected )
        {
            fill   = Mix( kPanel, kSelectFill, 0.85f );
            border = kAccent;
        }
        if ( missing )
        {
            fill   = kPanelDim;
            border = Fade( kError, 0.30f );
        }
        Box( r, fill, border, rounding );

        const float thumbHeight = r.W() * 9.0f / 16.0f;
        const Rect  thumb{ r.x0, r.y0, r.x1, r.y0 + thumbHeight };

        if ( missing )
        {
            dl->AddRectFilled( thumb.Min(), thumb.Max(), Mix( kPanelDim, IM_COL32_BLACK, 0.35f ), rounding,
                               ImDrawFlags_RoundCornersTop );
            const float size = BigIcon->FontSize * 0.44f;
            dl->AddText( BigIcon, size,
                         ImVec2( ( thumb.x0 + thumb.x1 ) * 0.5f - size * 0.5f, thumb.y0 + thumbHeight * 0.24f ),
                         Fade( kError, 0.55f ), ICON_ALERT_CIRCLE );
            // The PATH goes where the picture would have been. On a machine with three dead worktree
            // entries all called "Sandbox", the path is the only thing that tells them apart — and
            // this tile has space that will never hold a thumbnail.
            dl->PushClipRect( thumb.Min(), thumb.Max(), true );
            Wrapped( ImVec2( thumb.x0 + 12, thumb.y0 + thumbHeight * 0.56f ), kTextFaint, entry.Path.c_str(),
                     thumb.W() - 24.0f, Tiny );
            dl->PopClipRect();
        }
        else if ( const Hub::ThumbnailCache::Texture picture = st.Thumbnails.Get( entry.ThumbnailPath );
                  picture.Ok() )
        {
            // Cover-fit: crop, never letterbox — a letterboxed tile reads as a broken image. The
            // Editor writes 16:9 and the tile is 16:9, so in practice this crops nothing; it is here
            // for the file somebody drops in by hand.
            const float want = thumb.W() / thumb.H();
            const float have = static_cast<float>( picture.Width ) / static_cast<float>( picture.Height );
            ImVec2      uv0( 0, 0 ), uv1( 1, 1 );
            if ( have > want )
            {
                const float k = want / have;
                uv0.x         = ( 1 - k ) * 0.5f;
                uv1.x         = 1 - uv0.x;
            }
            else
            {
                const float k = have / want;
                uv0.y         = ( 1 - k ) * 0.5f;
                uv1.y         = 1 - uv0.y;
            }
            const ImU32 tint = state == TileState::Hover ? IM_COL32_WHITE : IM_COL32( 255, 255, 255, 240 );
            dl->AddImageRounded( picture.Id, thumb.Min(), thumb.Max(), uv0, uv1, tint, rounding,
                                 ImDrawFlags_RoundCornersTop );
        }
        else
        {
            // No `.thumbnail.png` — the common case for a project nobody has saved since the Editor
            // learned to write one. An ICON on the panel colour, never a broken-image frame: the
            // tile is not claiming a picture failed, it is saying there is none.
            dl->AddRectFilled( thumb.Min(), thumb.Max(), Mix( kPanel, kInk, 0.5f ), rounding,
                               ImDrawFlags_RoundCornersTop );
            const float size = BigIcon->FontSize * 0.5f;
            dl->AddText( BigIcon, size,
                         ImVec2( ( thumb.x0 + thumb.x1 ) * 0.5f - size * 0.5f,
                                 ( thumb.y0 + thumb.y1 ) * 0.5f - size * 0.62f ),
                         Fade( kTextFaint, 0.55f ), ICON_IMAGE_OFF );
        }
        dl->AddLine( ImVec2( r.x0, thumb.y1 ), ImVec2( r.x1, thumb.y1 ), Fade( IM_COL32_BLACK, 0.5f ) );

        const float footX     = r.x0 + 12.0f;
        const float footWidth = r.W() - 24.0f;
        Elide( ImVec2( footX, thumb.y1 + 10 ), missing ? kTextDim : kText, entry.Name.c_str(), footWidth, Bold );

        TileHit hit = TileHit::Body;
        if ( missing )
        {
            // The verbatim reason, and no Open anywhere on the tile — not a greyed one. There is no
            // version of that click that does anything.
            const char* remove      = ICON_TRASH "  Remove";
            const float removeWidth = TextW( remove, Small ) + 20.0f;
            const Rect  removeRect{ r.x1 - 12 - removeWidth, thumb.y1 + 26, r.x1 - 12, thumb.y1 + 50 };
            Box( removeRect, Fade( kError, 0.12f ), Fade( kError, 0.5f ), 3.0f );
            TextAt( ImVec2( removeRect.x0 + 10, removeRect.y0 + 5 ), kError, remove, Small );
            if ( removeRect.Contains( ImGui::GetIO().MousePos ) )
                hit = TileHit::Open; // "the one action this tile has" — mapped below

            TextAt( ImVec2( footX, thumb.y1 + 31 ), kError, ICON_ALERT, Small );
            Elide( ImVec2( footX + 18, thumb.y1 + 31 ), kError, entry.Trouble.c_str(),
                   footWidth - 18.0f - removeWidth - 10.0f, Small );
        }
        else
        {
            const std::string when = Hub::RelativeTime( entry.LastOpened, now );
            if ( !when.empty() )
            {
                TextAt( ImVec2( footX, thumb.y1 + 33 ), kTextFaint, ICON_CLOCK, Small );
                TextAt( ImVec2( footX + 18, thumb.y1 + 33 ), kTextDim, when.c_str(), Small );
            }
            else
            {
                // The registry has no time for this entry — every line migrated from the flat format
                // is like that. Say so, rather than inventing a date or leaving a blank nobody can
                // interpret.
                TextAt( ImVec2( footX, thumb.y1 + 33 ), kTextFaint, ICON_CLOCK, Small );
                TextAt( ImVec2( footX + 18, thumb.y1 + 33 ), kTextFaint, "not opened from here yet", Small );
            }
        }

        if ( state == TileState::Hover && !missing )
        {
            // Hover puts the primary verb ON the picture; the tile is still double-clickable.
            const Rect band{ thumb.x0, thumb.y1 - 40, thumb.x1, thumb.y1 };
            dl->PushClipRect( thumb.Min(), thumb.Max(), true );
            dl->AddRectFilled( band.Min(), band.Max(), IM_COL32( 8, 8, 10, 205 ) );
            dl->PopClipRect();

            const char* open      = ICON_ROCKET "  Open";
            const float openWidth = TextW( open, Small ) + 22.0f;
            const Rect  openRect{ band.x0 + 10, band.y0 + 8, band.x0 + 10 + openWidth, band.y1 - 8 };
            Box( openRect, kAccent, 0, 3.0f );
            TextAt( ImVec2( openRect.x0 + 11, openRect.y0 + 4 ), kOnAccent, open, Small );

            const Rect menuRect{ band.x1 - 36, band.y0 + 8, band.x1 - 10, band.y1 - 8 };
            Box( menuRect, Fade( IM_COL32_WHITE, 0.10f ), Fade( IM_COL32_WHITE, 0.18f ), 3.0f );
            TextAt( ImVec2( menuRect.x0 + 6, menuRect.y0 + 3 ), kText, ICON_DOTS, Small );

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if ( openRect.Contains( mouse ) )
                hit = TileHit::Open;
            else if ( menuRect.Contains( mouse ) )
                hit = TileHit::Menu;
        }
        return hit;
    }

    // ────────────────────────────────────────────────────────────── the projects screen

    bool MatchesSearch( const Hub::ProjectEntry& entry, const std::string& needle )
    {
        if ( needle.empty() )
            return true;
        const auto lower = []( std::string s )
        {
            std::transform( s.begin(), s.end(), s.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return s;
        };
        // The PATH is searched as well as the name, because on a machine with four projects called
        // Sandbox the path is the only thing that tells them apart.
        return lower( entry.Name ).find( lower( needle ) ) != std::string::npos ||
               lower( entry.Path ).find( lower( needle ) ) != std::string::npos;
    }

    void DrawEmptyState( Rect area, const char* icon, const char* title, const char* hint )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cx = ( area.x0 + area.x1 ) * 0.5f;
        const float cy = area.y0 + area.H() * 0.34f;

        const float size = BigIcon->FontSize * 0.8f;
        dl->AddText( BigIcon, size, ImVec2( cx - size * 0.5f, cy ), Fade( kTextFaint, 0.5f ), icon );
        TextAt( ImVec2( cx - TextW( title, Title ) * 0.5f, cy + 70.0f ), kText, title, Title );
        TextAt( ImVec2( cx - TextW( hint, Regular ) * 0.5f, cy + 100.0f ), kTextDim, hint );
    }

    void DrawProjectsView( HubState& st, const Frame& frame )
    {
        const Rect c = frame.Content;

        TextAt( ImVec2( c.x0, c.y0 - 2 ), kText, "Projects", H1 );

        // Right-hand actions, laid out from the right edge so a narrow window drops the search field
        // width rather than pushing the buttons off screen.
        const float top = c.y0 + 2.0f;
        float       x   = c.x1;
        {
            const char* create      = ICON_PLUS "  New Project";
            const float createWidth = TextW( create, Regular ) + 26.0f;
            x -= createWidth;
            if ( Button( ImVec2( x, top ), create, BtnKind::Accent, createWidth ) )
                st.Screen = View::NewProject;

            const char* open      = ICON_FOLDER_OPEN "  Open...";
            const float openWidth = TextW( open, Regular ) + 26.0f;
            x -= openWidth + 8.0f;
            if ( Button( ImVec2( x, top ), open, BtnKind::Solid, openWidth ) )
                OpenExisting( st );

            const float searchWidth = std::min( 230.0f, std::max( 120.0f, x - c.x0 - 200.0f ) );
            x -= searchWidth + 10.0f;
            ImGui::SetCursorScreenPos( ImVec2( x, top ) );
            ImGui::SetNextItemWidth( searchWidth );
            ImGui::PushStyleVar( ImGuiStyleVar_FramePadding,
                                 ImVec2( 30.0f, ( 32.0f - Regular->FontSize ) * 0.5f ) );
            ImGui::PushStyleColor( ImGuiCol_Border, V4( kBorderSoft ) );
            ImGui::InputTextWithHint( "##search", "Search projects", st.Search, sizeof( st.Search ) );
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            TextAt( ImVec2( x + 9, top + ( 32.0f - Regular->FontSize ) * 0.5f ), kTextFaint, ICON_MAGNIFY );
        }

        // The header does NOT scroll; only the grid below it does.
        //
        // The grid gets the WHOLE content column, and the scrollbar lives out in the window's right
        // padding — so it appears and disappears without a single tile changing width. The mock's
        // own renderer takes 13 px off this rectangle BEFORE applying the column rule, and that is
        // a slip in the mock rather than the design: subtracting it gives 3 columns and a 319 px
        // tile at the design width, which contradicts every number the design states. Screen 04
        // says "content 1002 px, floor(1002/250) = 4 columns, tile 240 x 191" and "content 622 px,
        // 2 columns, tile 303 x 226"; the full content width reproduces both exactly, including the
        // 190/191 px tile height, and the 13-px-narrower rectangle reproduces neither.
        const Rect area{ c.x0, c.y0 + 56.0f, c.x1, c.y1 };

        std::vector<int> visible;
        visible.reserve( st.Entries.size() );
        for ( int i = 0; i < static_cast<int>( st.Entries.size() ); ++i )
            if ( MatchesSearch( st.Entries[i], st.Search ) )
                visible.push_back( i );

        // The count beside the title counts what is ON SCREEN. With a search typed it says "1 of 14"
        // rather than "14 projects" over a grid holding one — a number that describes something the
        // user cannot see is worse than no number.
        {
            char count[64];
            if ( visible.size() == st.Entries.size() )
                std::snprintf( count, sizeof( count ), "%d project%s", static_cast<int>( st.Entries.size() ),
                               st.Entries.size() == 1 ? "" : "s" );
            else
                std::snprintf( count, sizeof( count ), "%d of %d", static_cast<int>( visible.size() ),
                               static_cast<int>( st.Entries.size() ) );
            TextAt( ImVec2( c.x0 + TextW( "Projects", H1 ) + 12, c.y0 + 10 ), kTextFaint, count, Small );
        }

        if ( st.Entries.empty() )
        {
            DrawEmptyState( area, ICON_VIEW_GRID, "No projects yet",
                            "Create one from a template, or open a .deproj you already have." );
            const float cx          = ( area.x0 + area.x1 ) * 0.5f;
            const float cy          = area.y0 + area.H() * 0.34f + 140.0f;
            const char* createLabel = ICON_PLUS "  Create New";
            const char* openLabel   = ICON_FOLDER_OPEN "  Open existing";
            const float createWidth = TextW( createLabel, Regular ) + 26.0f;
            const float openWidth   = TextW( openLabel, Regular ) + 26.0f;
            const float total       = createWidth + 10.0f + openWidth;
            if ( Button( ImVec2( cx - total * 0.5f, cy ), createLabel, BtnKind::Accent, createWidth, 36.0f ) )
                st.Screen = View::NewProject;
            if ( Button( ImVec2( cx - total * 0.5f + createWidth + 10.0f, cy ), openLabel, BtnKind::Solid,
                         openWidth, 36.0f ) )
                OpenExisting( st );
            return;
        }
        if ( visible.empty() )
        {
            // The same layout, a different sentence. A search with no hits is not an empty machine,
            // and offering "Create New" here would answer a question nobody asked.
            DrawEmptyState( area, ICON_MAGNIFY, "Nothing matches that",
                            "No project name or path contains what you typed." );
            return;
        }

        const int columns = Hub::GridColumns( area.W() );
        float     tileWidth =
             ( area.W() - kTileGap * static_cast<float>( columns - 1 ) ) / static_cast<float>( columns );
        tileWidth              = std::min( tileWidth, kTileMaxWidth );
        const float tileHeight = std::floor( tileWidth * 9.0f / 16.0f ) + 56.0f;

        const int   rows     = ( static_cast<int>( visible.size() ) + columns - 1 ) / columns;
        const float contentH = static_cast<float>( rows ) * tileHeight + static_cast<float>( rows - 1 ) * kTileGap;
        const float maxScroll = std::max( 0.0f, contentH - area.H() );

        ImGuiIO& io = ImGui::GetIO();
        if ( area.Contains( io.MousePos ) && io.MouseWheel != 0.0f && !ImGui::IsAnyItemActive() )
            st.Scroll -= io.MouseWheel * 60.0f;
        st.Scroll = std::clamp( st.Scroll, 0.0f, maxScroll );

        // A click only counts as a click on a TILE when ImGui does not already own the mouse — the
        // search field, a button and an open popup all sit above this grid.
        const bool imguiOwnsMouse =
             ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive() ||
             ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel );
        const bool mouseInGrid = area.Contains( io.MousePos ) && !imguiOwnsMouse;

        int  openIndex = -1, removeIndex = -1, settingsIndex = -1, revealIndex = -1;
        bool openMenu = false;
        // One clock read per frame, not one per tile: `now` is the same instant for every tile on
        // screen, and twelve syscalls a frame to learn that would be twelve too many.
        const long long now = Common::Project::UnixNow();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect( area.Min(), area.Max(), true );
        for ( int slot = 0; slot < static_cast<int>( visible.size() ); ++slot )
        {
            const int  index = visible[slot];
            const int  row = slot / columns, column = slot % columns;
            const Rect tile{ area.x0 + static_cast<float>( column ) * ( tileWidth + kTileGap ),
                             area.y0 + static_cast<float>( row ) * ( tileHeight + kTileGap ) - st.Scroll, 0, 0 };
            const Rect r{ tile.x0, tile.y0, tile.x0 + tileWidth, tile.y0 + tileHeight };
            if ( r.y1 < area.y0 - 4.0f || r.y0 > area.y1 + 4.0f )
                continue; // off screen: not drawn, and — crucially — no thumbnail decode requested

            const bool hovered = mouseInGrid && r.Contains( io.MousePos );
            if ( hovered )
            {
                // Where Description is READ. It is not on the face of the tile — the design gives
                // the tile a name and a time and nothing else — but a field with no consumer is a
                // field nobody should have added, so the hover says it along with the path.
                ImGui::BeginTooltip();
                // Wrapped, and hard: a project inside an agent worktree has a path that would
                // otherwise draw a tooltip wider than the window it is explaining.
                ImGui::PushTextWrapPos( 420.0f );
                if ( !st.Entries[index].Description.empty() )
                {
                    ImGui::TextUnformatted( st.Entries[index].Description.c_str() );
                    ImGui::Spacing();
                }
                ImGui::TextColored( V4( kTextFaint ), "%s", st.Entries[index].Path.c_str() );
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            TileState state = TileState::Normal;
            if ( !st.Entries[index].IsOpenable() )
                state = TileState::Missing;
            else if ( hovered )
                state = TileState::Hover;
            else if ( index == st.Selected )
                state = TileState::Selected;

            const TileHit hit = DrawProjectTile( st, r, st.Entries[index], state, now );
            if ( !hovered )
                continue;

            if ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
            {
                st.MenuIndex = index;
                st.Selected  = index;
                openMenu     = true;
            }
            else if ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
            {
                st.Selected = index;
                if ( hit == TileHit::Open )
                {
                    // On a missing tile the one control in that position is Remove, and it is the
                    // only thing a missing tile can be asked to do.
                    if ( st.Entries[index].IsOpenable() )
                        openIndex = index;
                    else
                        removeIndex = index;
                }
                else if ( hit == TileHit::Menu )
                {
                    st.MenuIndex = index;
                    openMenu     = true;
                }
            }
            else if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && st.Entries[index].IsOpenable() )
            {
                openIndex = index;
            }
        }
        dl->PopClipRect();

        if ( maxScroll > 0.0f )
        {
            // Out in the window's 24 px right padding, not inside the grid: a scrollbar that took
            // width from the tiles would reflow the whole page the moment a fourteenth project
            // appeared.
            const float trackX   = area.x1 + 9.0f;
            const float length   = std::max( 40.0f, area.H() * ( area.H() / contentH ) );
            const float position = area.y0 + ( area.H() - length ) * ( st.Scroll / maxScroll );
            dl->AddRectFilled( ImVec2( trackX, area.y0 ), ImVec2( trackX + 5, area.y1 ),
                               IM_COL32( 255, 255, 255, 12 ), 2.5f );
            dl->AddRectFilled( ImVec2( trackX, position ), ImVec2( trackX + 5, position + length ),
                               IM_COL32( 255, 255, 255, 60 ), 2.5f );
        }

        if ( openMenu )
            ImGui::OpenPopup( "##tilemenu" );
        if ( ImGui::BeginPopup( "##tilemenu" ) )
        {
            if ( st.MenuIndex >= 0 && st.MenuIndex < static_cast<int>( st.Entries.size() ) )
            {
                const Hub::ProjectEntry& entry = st.Entries[st.MenuIndex];
                ImGui::TextColored( V4( kTextFaint ), "%s", entry.Name.c_str() );
                ImGui::Separator();
                // Open is ABSENT on an unopenable entry, not greyed: a control that only ever
                // refuses is worse than no control.
                if ( entry.IsOpenable() && ImGui::MenuItem( ICON_ROCKET "  Open" ) )
                    openIndex = st.MenuIndex;
                if ( ImGui::MenuItem( ICON_FOLDER_OPEN "  Reveal in Finder" ) )
                    revealIndex = st.MenuIndex;
                if ( entry.IsOpenable() && ImGui::MenuItem( ICON_COG "  Settings" ) )
                    settingsIndex = st.MenuIndex;
                ImGui::Separator();
                if ( ImGui::MenuItem( ICON_TRASH "  Remove from list" ) )
                    removeIndex = st.MenuIndex;
                ImGui::TextColored( V4( kTextFaint ), "the files stay on disk" );
            }
            ImGui::EndPopup();
        }

        // Applied AFTER the loop: erasing while drawing shifts every tile below the removed one into
        // a different index for the rest of the frame.
        if ( openIndex >= 0 )
            OpenProject( st, st.Entries[openIndex] );
        else if ( revealIndex >= 0 )
            RevealProject( st, st.Entries[revealIndex] );
        else if ( settingsIndex >= 0 )
            EnterSettings( st, settingsIndex );
        else if ( removeIndex >= 0 )
            RemoveEntry( st, removeIndex );
    }

    // ────────────────────────────────────────────────────────────── new project

    bool BackLink( ImVec2 at, const char* label )
    {
        const float width = TextW( label, Small ) + 22.0f;
        ImGui::SetCursorScreenPos( at );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, V4( Fade( kText, 0.08f ) ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, V4( Fade( kText, 0.14f ) ) );
        ImGui::PushStyleColor( ImGuiCol_Text, V4( kTextDim ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        const bool pressed = ImGui::Button( label, ImVec2( width, 24.0f ) );
        ImGui::PopStyleVar();
        ImGui::PopStyleColor( 4 );
        return pressed;
    }

    void DrawTemplateCard( HubState& st, Rect r, const Hub::TemplateEntry& entry, bool selected )
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        Box( r, selected ? Mix( kPanel, kSelectFill, 0.85f ) : kPanel, selected ? kAccent : kBorderSoft, 6.0f );

        const Rect thumb{ r.x0, r.y0, r.x1, r.y0 + r.W() * 9.0f / 16.0f };
        if ( const Hub::ThumbnailCache::Texture picture = st.Thumbnails.Get( entry.ThumbnailPath ); picture.Ok() )
        {
            dl->AddImageRounded( picture.Id, thumb.Min(), thumb.Max(), ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                                 IM_COL32_WHITE, 6.0f, ImDrawFlags_RoundCornersTop );
        }
        else
        {
            // A template with no Media/Thumbnail.png. The file is a convention, not a field, so its
            // absence is a picture-less card and nothing else.
            dl->AddRectFilled( thumb.Min(), thumb.Max(), Mix( kPanel, kInk, 0.5f ), 6.0f,
                               ImDrawFlags_RoundCornersTop );
            const float size = BigIcon->FontSize * 0.45f;
            dl->AddText( BigIcon, size,
                         ImVec2( ( thumb.x0 + thumb.x1 ) * 0.5f - size * 0.5f,
                                 ( thumb.y0 + thumb.y1 ) * 0.5f - size * 0.6f ),
                         Fade( kTextFaint, 0.5f ), ICON_PACKAGE );
        }

        TextAt( ImVec2( r.x0 + 14, thumb.y1 + 12 ), selected ? kAccent : kText, entry.Manifest.DisplayName.c_str(),
                Bold );
        Wrapped( ImVec2( r.x0 + 14, thumb.y1 + 34 ), kTextDim, entry.Manifest.Description.c_str(), r.W() - 28.0f,
                 Small );

        if ( selected )
        {
            dl->AddCircleFilled( ImVec2( r.x1 - 18, r.y0 + 18 ), 9.0f, kAccent, 16 );
            TextAt( ImVec2( r.x1 - 24, r.y0 + 11 ), kOnAccent, ICON_CHECK, Small );
        }
    }

    void DrawNewProjectView( HubState& st, const Frame& frame )
    {
        const Rect c = frame.Content;

        if ( BackLink( ImVec2( c.x0 - 6, c.y0 - 6 ), ICON_CHEVRON_LEFT "  Projects" ) )
            st.Screen = View::Projects;
        TextAt( ImVec2( c.x0, c.y0 + 24 ), kText, "New Project", H1 );

        const float bodyTop = c.y0 + 74.0f;
        const float leftW   = std::min( 575.0f, c.W() * 0.5f - 12.0f );
        const float rightX  = c.x0 + leftW + 34.0f;
        const float rightW  = c.x1 - rightX;

        // ── left: the templates, as they are on disk ──
        SectionLabel( ImVec2( c.x0, bodyTop - 18 ), "TEMPLATE" );

        const auto& templates = st.TemplateScan.Templates;
        st.Template = std::clamp( st.Template, 0, std::max( 0, static_cast<int>( templates.size() ) - 1 ) );

        constexpr float kCardGap    = 18.0f;
        const float     cardW       = ( leftW - kCardGap ) * 0.5f;
        const float     cardH       = std::floor( cardW * 9.0f / 16.0f ) + 96.0f;
        float           cardsBottom = bodyTop;

        const ImVec2 mouse          = ImGui::GetIO().MousePos;
        const bool   imguiOwnsMouse = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

        // The scan comes back ordered by Category first, so a heading whenever the category CHANGES
        // is the whole of grouping. This is where TemplateManifest::Category is READ — a manifest
        // field nothing consumed would be a dead setting, and the launcher would be asking template
        // authors to fill in something it throws away. Every template shipped today declares "", so
        // no heading is drawn and the layout is a plain grid; a template that names a category
        // groups itself the moment its folder is dropped in, with no code change here.
        std::string currentCategory;
        float       rowTop      = bodyTop;
        int         columnIndex = 0;
        for ( int i = 0; i < static_cast<int>( templates.size() ); ++i )
        {
            if ( templates[i].Manifest.Category != currentCategory )
            {
                currentCategory = templates[i].Manifest.Category;
                if ( columnIndex != 0 ) // finish the half-filled row before the new heading
                {
                    rowTop += cardH + kCardGap;
                    columnIndex = 0;
                }
                if ( !currentCategory.empty() )
                {
                    SectionLabel( ImVec2( c.x0, rowTop + 4 ), currentCategory.c_str() );
                    rowTop += 24.0f;
                }
            }

            const float left = c.x0 + static_cast<float>( columnIndex ) * ( cardW + kCardGap );
            const Rect  card{ left, rowTop, left + cardW, rowTop + cardH };
            DrawTemplateCard( st, card, templates[i], i == st.Template );
            if ( !imguiOwnsMouse && card.Contains( mouse ) && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
                st.Template = i;
            cardsBottom = std::max( cardsBottom, card.y1 );

            if ( ++columnIndex == 2 )
            {
                columnIndex = 0;
                rowTop += cardH + kCardGap;
            }
        }

        if ( templates.empty() )
        {
            const Rect box{ c.x0, bodyTop, c.x0 + leftW, bodyTop + 96.0f };
            Box( box, Fade( kWarning, 0.08f ), Fade( kWarning, 0.4f ), 5.0f );
            TextAt( ImVec2( box.x0 + 12, box.y0 + 12 ), kWarning, ICON_ALERT, Small );
            Wrapped( ImVec2( box.x0 + 32, box.y0 + 12 ), kWarning,
                     "No templates were found on this engine install, so there is nothing to create a project "
                     "from. Templates are folders under <engine-root>/Templates with a template.json in them.",
                     box.W() - 44.0f, Small );
            cardsBottom = box.y1;
        }

        // Every folder that did NOT load, with the parser's own words. A silent skip would mean a
        // template that exists on disk and nowhere on screen (L2 §3.4).
        float refusalY = cardsBottom + 16.0f;
        for ( const std::string& refusal : st.TemplateScan.Refusals )
        {
            const float height = 44.0f;
            const Rect  box{ c.x0, refusalY, c.x0 + leftW, refusalY + height };
            if ( box.y1 > c.y1 )
                break;
            Box( box, Fade( kWarning, 0.08f ), Fade( kWarning, 0.35f ), 5.0f );
            TextAt( ImVec2( box.x0 + 12, box.y0 + 11 ), kWarning, ICON_ALERT, Small );
            Wrapped( ImVec2( box.x0 + 32, box.y0 + 8 ), kWarning, refusal.c_str(), box.W() - 44.0f, Small );
            refusalY = box.y1 + 8.0f;
        }
        if ( refusalY + 16.0f < c.y1 )
            TextAt( ImVec2( c.x0, refusalY + 4 ), kTextFaint,
                    "Templates are folders under the engine install. Adding one is dropping a folder in.", Small );

        // ── right: the two fields, and what will be on disk when this finishes ──
        SectionLabel( ImVec2( rightX, bodyTop - 18 ), "PROJECT NAME" );
        InputField( Rect{ rightX, bodyTop, c.x1, bodyTop + 34.0f }, "##name", st.NewName, sizeof( st.NewName ) );

        float y = bodyTop + 44.0f;
        // Said WHILE TYPING, not after Create, and by the same rule CreateProject enforces.
        const std::string nameProblem = Hub::ValidateProjectName( st.NewName );
        if ( !nameProblem.empty() )
        {
            TextAt( ImVec2( rightX, y ), kError, nameProblem.c_str(), Small );
            y += 20.0f;
        }

        SectionLabel( ImVec2( rightX, y + 4 ), "LOCATION" );
        y += 22.0f;
        const char* browse      = ICON_FOLDER_OPEN "  Browse...";
        const float browseWidth = TextW( browse, Regular ) + 26.0f;
        InputField( Rect{ rightX, y, c.x1 - browseWidth - 10.0f, y + 34.0f }, "##location", st.NewLocation,
                    sizeof( st.NewLocation ) );
        if ( Button( ImVec2( c.x1 - browseWidth, y + 1 ), browse, BtnKind::Solid, browseWidth ) )
        {
            const std::string chosen =
                 Hub::FileDialog::PickDirectory( "Where to create the project", st.NewLocation );
            if ( !chosen.empty() ) // "" is a cancel, and a cancel must not wipe the field
                std::snprintf( st.NewLocation, sizeof( st.NewLocation ), "%s", chosen.c_str() );
        }
        y += 44.0f;

        // Name + Location is the one place a launcher can create a folder somewhere the user did not
        // mean, so the resolved path is spelled out rather than left to be assembled in their head.
        {
            const std::string resolved =
                 ( fs::path( st.NewLocation ) / st.NewName ).lexically_normal().string() + "/";
            TextAt( ImVec2( rightX, y ), kTextFaint, "Creates", Small );
            ElideFront( ImVec2( rightX + TextW( "Creates", Small ) + 10.0f, y ), kAccent, resolved.c_str(),
                        rightW - TextW( "Creates", Small ) - 10.0f, Small );
            y += 26.0f;
        }

        // "What you get" — read out of the manifest and the shared folder census. Not a marketing
        // list: every row is a path that will exist when the button finishes.
        {
            const Rect box{ rightX, y, c.x1, std::min( c.y1 - 52.0f, y + 190.0f ) };
            if ( box.H() > 60.0f )
            {
                Box( box, kPanel, kBorderSoft, 5.0f );
                TextAt( ImVec2( box.x0 + 14, box.y0 + 12 ), kText, "What you get", Bold );
                float rowY = box.y0 + 40.0f;
                if ( !templates.empty() )
                {
                    const Hub::TemplateEntry& chosen = templates[st.Template];
                    TextAt( ImVec2( box.x0 + 14 + TextW( "What you get", Bold ) + 10.0f, box.y0 + 15 ), kTextFaint,
                            ( "from Templates/" + chosen.Id + "/template.json" ).c_str(), Tiny );

                    const std::string descriptor = std::string( st.NewName ) + ".deproj";
                    TextAt( ImVec2( box.x0 + 18, rowY ), kTextDim, descriptor.c_str(), Small );
                    TextAt( ImVec2( box.x0 + 18, rowY + 16 ), kTextFaint,
                            "descriptor: Name, AssetsRoot, DefaultScene, EngineVersion, FileVersion", Tiny );
                    rowY += 38.0f;

                    if ( !chosen.Manifest.DefaultScene.empty() )
                    {
                        TextAt( ImVec2( box.x0 + 18, rowY ), kTextDim, chosen.Manifest.DefaultScene.c_str(),
                                Small );
                        TextAt( ImVec2( box.x0 + 18, rowY + 16 ), kTextFaint, "the template's default scene",
                                Tiny );
                        rowY += 38.0f;
                    }

                    std::string census;
                    for ( const std::string_view folder : Common::Project::StandardContentFolders )
                        census += "Assets/" + std::string( folder ) + "  ";
                    Wrapped( ImVec2( box.x0 + 18, rowY ), kTextDim, census.c_str(), box.W() - 36.0f, Small );
                    TextAt( ImVec2( box.x0 + 18, rowY + 32 ), kTextFaint, "the standard content folders", Tiny );
                    rowY += 54.0f;
                    if ( rowY + 14.0f < box.y1 )
                        TextAt( ImVec2( box.x0 + 18, rowY ), kTextFaint,
                                "plus Payload/ copied byte-for-byte - no substitutions", Tiny );
                }
                y = box.y1 + 12.0f;
            }
        }

        // ── the two buttons ──
        {
            const char* create      = ICON_ROCKET "  Create & Open";
            const float createWidth = TextW( create, Regular ) + 30.0f;
            const float cancelWidth = TextW( "Cancel", Regular ) + 30.0f;
            const float buttonsY    = c.y1 - 40.0f;

            const bool ready = nameProblem.empty() && !templates.empty() && st.NewLocation[0] != '\0';
            if ( Button( ImVec2( c.x1 - createWidth, buttonsY ), create,
                         ready ? BtnKind::Accent : BtnKind::Disabled, createWidth, 38.0f ) &&
                 ready )
            {
                auto created = Hub::CreateProject( st.NewLocation, st.NewName, templates[st.Template],
                                                   st.Engine.VersionFull );
                if ( created.IsSuccess() )
                {
                    const std::string deproj = created.ExtractValue();
                    Common::Project::PromoteRecent( st.Registry, deproj, Common::Project::UnixNow() );
                    PersistRegistry( st );
                    OpenProject( st, Hub::ResolveProjectEntry( deproj ) );
                }
                else
                {
                    // Verbatim, and it says what did NOT happen as well as what failed: the
                    // half-made folder was removed, so the world is not in an unknown state.
                    st.SetError( created.GetError() +
                                 " Nothing was left behind - the half-made folder was removed." );
                }
            }
            if ( Button( ImVec2( c.x1 - createWidth - cancelWidth - 10.0f, buttonsY ), "Cancel", BtnKind::Solid,
                         cancelWidth, 38.0f ) )
                st.Screen = View::Projects;
        }
    }

    // ────────────────────────────────────────────────────────────── project settings

    std::vector<std::string> ScanScenes( const std::string& projectDirectory, const std::string& assetsRoot )
    {
        std::vector<std::string> scenes;
        std::error_code          ec;
        const fs::path           root = fs::path( projectDirectory ) / assetsRoot / "Scenes";
        if ( !fs::exists( root, ec ) )
            return scenes;
        for ( fs::recursive_directory_iterator it( root, ec ), end; it != end && !ec; it.increment( ec ) )
            if ( it->is_regular_file( ec ) && it->path().extension() == ".desce" )
                scenes.push_back( fs::relative( it->path(), projectDirectory, ec ).generic_string() );
        std::sort( scenes.begin(), scenes.end() );
        return scenes;
    }

    std::string MeasureSizeOnDisk( const std::string& projectDirectory )
    {
        std::error_code ec;
        std::uintmax_t  total = 0;
        for ( fs::recursive_directory_iterator it( projectDirectory, ec ), end; it != end && !ec;
              it.increment( ec ) )
            if ( it->is_regular_file( ec ) )
                total += it->file_size( ec );

        char text[64];
        if ( total >= 1024ull * 1024 * 1024 )
            std::snprintf( text, sizeof( text ), "%.2f GB",
                           static_cast<double>( total ) / ( 1024.0 * 1024 * 1024 ) );
        else if ( total >= 1024ull * 1024 )
            std::snprintf( text, sizeof( text ), "%.1f MB", static_cast<double>( total ) / ( 1024.0 * 1024 ) );
        else
            std::snprintf( text, sizeof( text ), "%.0f KB", static_cast<double>( total ) / 1024.0 );
        return text;
    }

    void EnterSettings( HubState& st, int index )
    {
        st.SettingsIndex = index;
        st.Screen        = View::Settings;
        st.SettingsDirty = false;
        st.SizeOnDisk.clear();
        st.SizeMeasuredAt = -1.0;

        const Hub::ProjectEntry& entry = st.Entries[index];
        std::snprintf( st.EditName, sizeof( st.EditName ), "%s", entry.Name.c_str() );
        std::snprintf( st.EditDescription, sizeof( st.EditDescription ), "%s", entry.Description.c_str() );

        // Read the descriptor again rather than reusing what the tile happened to keep: this screen
        // WRITES the file back, and writing from a partial copy is how fields get dropped.
        const auto raw = Hub::ReadTextFile( entry.Path );
        if ( raw.IsSuccess() )
            if ( auto parsed = Common::Project::ReadProjectFile( raw.GetValue() ); parsed.IsSuccess() )
            {
                st.Descriptor       = parsed.ExtractValue();
                st.EditDefaultScene = st.Descriptor.DefaultScene;
                st.Scenes = ScanScenes( fs::path( entry.Path ).parent_path().string(), st.Descriptor.AssetsRoot );
            }
    }

    void SaveSettings( HubState& st )
    {
        const Hub::ProjectEntry& entry = st.Entries[st.SettingsIndex];

        // Read-modify-write through the shared serializer, so a field this screen does not show is
        // carried across instead of being erased by a writer that only knows about three of them.
        const auto raw = Hub::ReadTextFile( entry.Path );
        if ( !raw.IsSuccess() )
        {
            st.SetError( "Could not read " + entry.Path + ": " + raw.GetError() + " Nothing was written." );
            return;
        }
        auto parsed = Common::Project::ReadProjectFile( raw.GetValue() );
        if ( !parsed.IsSuccess() )
        {
            st.SetError( entry.Path + ": " + parsed.GetError() + " Nothing was written." );
            return;
        }

        Common::Project::ProjectFile descriptor = parsed.ExtractValue();
        descriptor.Name                         = st.EditName;
        descriptor.Description                  = st.EditDescription;
        descriptor.DefaultScene                 = st.EditDefaultScene;

        if ( const auto written =
                  Hub::WriteTextFile( entry.Path, Common::Project::WriteProjectFile( descriptor ) );
             !written.IsSuccess() )
        {
            st.SetError( "Could not write " + entry.Path + ": " + written.GetError() +
                         " The file on disk is unchanged." );
            return;
        }
        st.Descriptor    = descriptor; // the screen and the disk now agree, and both say the same thing
        st.SettingsDirty = false;
        st.ResolveEntries();
        st.SetNotice( "Saved " + entry.Path );
    }

    void LockedRow( Rect r, const char* label, const char* value )
    {
        TextAt( ImVec2( r.x0, r.y0 ), kTextFaint, ICON_LOCK, Small );
        TextAt( ImVec2( r.x0 + 20, r.y0 ), kTextDim, label, Small );
        Elide( ImVec2( r.x0 + 190, r.y0 ), kText, value, r.W() - 200.0f, Small );
    }

    void DrawSettingsView( HubState& st, const Frame& frame )
    {
        if ( st.SettingsIndex < 0 || st.SettingsIndex >= static_cast<int>( st.Entries.size() ) )
        {
            st.Screen = View::Projects;
            return;
        }
        const Rect               c         = frame.Content;
        const Hub::ProjectEntry& entry     = st.Entries[st.SettingsIndex];
        const std::string        directory = fs::path( entry.Path ).parent_path().string();

        if ( BackLink( ImVec2( c.x0 - 6, c.y0 - 8 ), ICON_CHEVRON_LEFT "  Projects" ) )
            st.Screen = View::Projects;

        // ── the thing these settings are FOR, at the top, with its Open still available ──
        const Rect thumb{ c.x0, c.y0 + 22, c.x0 + 180.0f, c.y0 + 22 + 101.0f };
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if ( const Hub::ThumbnailCache::Texture picture = st.Thumbnails.Get( entry.ThumbnailPath );
                 picture.Ok() )
                dl->AddImageRounded( picture.Id, thumb.Min(), thumb.Max(), ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                                     IM_COL32_WHITE, 5.0f );
            else
            {
                Box( thumb, Mix( kPanel, kInk, 0.5f ), kBorderSoft, 5.0f );
                const float size = BigIcon->FontSize * 0.45f;
                dl->AddText( BigIcon, size,
                             ImVec2( ( thumb.x0 + thumb.x1 ) * 0.5f - size * 0.5f,
                                     ( thumb.y0 + thumb.y1 ) * 0.5f - size * 0.6f ),
                             Fade( kTextFaint, 0.5f ), ICON_IMAGE_OFF );
            }
        }
        TextAt( ImVec2( thumb.x1 + 22, c.y0 + 26 ), kText, entry.Name.c_str(), H1 );
        ElideFront( ImVec2( thumb.x1 + 22, c.y0 + 62 ), kTextDim, entry.Path.c_str(), c.W() - 200.0f - 160.0f,
                    Small );

        {
            const char* open      = ICON_ROCKET "  Open";
            const float openWidth = TextW( open, Regular ) + 28.0f;
            if ( Button( ImVec2( c.x1 - openWidth, c.y0 + 24 ), open, BtnKind::Accent, openWidth, 34.0f ) )
                OpenProject( st, entry );
        }

        // ── PROJECT: the three fields this project actually has ──
        float y = thumb.y1 + 30.0f;
        SectionLabel( ImVec2( c.x0, y ), "PROJECT" );
        {
            const float saveWidth   = TextW( ICON_SAVE "  Save", Regular ) + 26.0f;
            const float revertWidth = TextW( "Revert", Regular ) + 26.0f;
            if ( Button( ImVec2( c.x1 - saveWidth, y - 8 ), ICON_SAVE "  Save",
                         st.SettingsDirty ? BtnKind::Accent : BtnKind::Disabled, saveWidth, 30.0f ) &&
                 st.SettingsDirty )
                SaveSettings( st );
            if ( Button( ImVec2( c.x1 - saveWidth - revertWidth - 8.0f, y - 8 ), "Revert",
                         st.SettingsDirty ? BtnKind::Solid : BtnKind::Disabled, revertWidth, 30.0f ) &&
                 st.SettingsDirty )
                EnterSettings( st, st.SettingsIndex );
            if ( st.SettingsDirty )
            {
                const float dot = TextW( "unsaved", Small );
                TextAt( ImVec2( c.x1 - saveWidth - revertWidth - 26.0f - dot, y - 2 ), kTextDim, "unsaved",
                        Small );
                ImGui::GetWindowDrawList()->AddCircleFilled(
                     ImVec2( c.x1 - saveWidth - revertWidth - 18.0f, y + 4 ), 3.5f, kAccent, 10 );
            }
        }

        y += 16.0f;
        const Rect panel{ c.x0, y, c.x1, y + 218.0f };
        Box( panel, kPanel, kBorderSoft, 5.0f );
        const float fieldX = panel.x0 + 336.0f;
        const float fieldW = panel.x1 - 18.0f - fieldX;

        {
            float rowY = panel.y0 + 18.0f;
            TextAt( ImVec2( panel.x0 + 18, rowY ), kText, "Name", Bold );
            Wrapped( ImVec2( panel.x0 + 18, rowY + 20 ), kTextFaint,
                     "written into the .deproj; the tile and the window title read it back", 300.0f, Tiny );
            if ( InputField( Rect{ fieldX, rowY - 4, fieldX + fieldW, rowY + 30 }, "##sname", st.EditName,
                             sizeof( st.EditName ) ) )
                st.SettingsDirty = true;
            rowY += 74.0f;

            TextAt( ImVec2( panel.x0 + 18, rowY ), kText, "Default Scene", Bold );
            Wrapped( ImVec2( panel.x0 + 18, rowY + 20 ), kTextFaint,
                     "scanned from Assets/Scenes/**/*.desce, the same scan the Editor's Build Settings does",
                     300.0f, Tiny );
            {
                ImGui::SetCursorScreenPos( ImVec2( fieldX, rowY - 4 ) );
                ImGui::SetNextItemWidth( fieldW );
                ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 10.0f, 8.0f ) );
                const std::string preview = st.EditDefaultScene.empty() ? "(none)" : st.EditDefaultScene;
                if ( ImGui::BeginCombo( "##sscene", preview.c_str() ) )
                {
                    if ( ImGui::Selectable( "(none)", st.EditDefaultScene.empty() ) )
                    {
                        st.EditDefaultScene = "";
                        st.SettingsDirty    = true;
                    }
                    for ( const std::string& scene : st.Scenes )
                        if ( ImGui::Selectable( scene.c_str(), scene == st.EditDefaultScene ) )
                        {
                            st.EditDefaultScene = scene;
                            st.SettingsDirty    = true;
                        }
                    ImGui::EndCombo();
                }
                ImGui::PopStyleVar();
            }
            rowY += 74.0f;

            TextAt( ImVec2( panel.x0 + 18, rowY ), kText, "Description", Bold );
            Wrapped( ImVec2( panel.x0 + 18, rowY + 20 ), kTextFaint,
                     "a new .deproj field whose only consumer is the project tile", 300.0f, Tiny );
            ImGui::SetCursorScreenPos( ImVec2( fieldX, rowY - 4 ) );
            ImGui::PushStyleColor( ImGuiCol_Border, V4( kBorderSoft ) );
            if ( ImGui::InputTextMultiline( "##sdesc", st.EditDescription, sizeof( st.EditDescription ),
                                            ImVec2( fieldW, 54.0f ) ) )
                st.SettingsDirty = true;
            ImGui::PopStyleColor();
        }

        // ── DESCRIPTOR: what the project IS, and the operations on it ──
        y = panel.y1 + 26.0f;
        SectionLabel( ImVec2( c.x0, y ), "DESCRIPTOR" );
        y += 16.0f;

        const float halfW = ( c.W() - 20.0f ) * 0.5f;
        const Rect  facts{ c.x0, y, c.x0 + halfW, y + 96.0f };
        Box( facts, kPanel, kBorderSoft, 5.0f );
        {
            // Shown, not hidden. "Engine version 0.1.316, file version 1" is the first thing anyone
            // wants when a project refuses to open on another machine.
            const Common::Project::ProjectFile& descriptor = st.Descriptor;
            LockedRow( Rect{ facts.x0 + 16, facts.y0 + 16, facts.x1 - 16, 0 }, "Engine version",
                       descriptor.EngineVersion.empty() ? "(never written by an Editor)"
                                                        : descriptor.EngineVersion.c_str() );
            LockedRow( Rect{ facts.x0 + 16, facts.y0 + 42, facts.x1 - 16, 0 }, "File version",
                       descriptor.FileVersion == 0 ? "0 (written before .deproj carried one)"
                                                   : std::to_string( descriptor.FileVersion ).c_str() );
            LockedRow( Rect{ facts.x0 + 16, facts.y0 + 68, facts.x1 - 16, 0 }, "Assets root",
                       descriptor.AssetsRoot.c_str() );
        }

        const Rect operations{ c.x1 - halfW, y, c.x1, y + 96.0f };
        Box( operations, kPanel, kBorderSoft, 5.0f );
        {
            TextAt( ImVec2( operations.x0 + 16, operations.y0 + 16 ), kTextFaint, ICON_INFO, Small );
            TextAt( ImVec2( operations.x0 + 36, operations.y0 + 16 ), kTextDim, "Size on disk", Small );
            if ( st.SizeOnDisk.empty() )
                TextAt( ImVec2( operations.x0 + 150, operations.y0 + 16 ), kTextFaint, "not measured", Small );
            else
            {
                TextAt( ImVec2( operations.x0 + 150, operations.y0 + 16 ), kText, st.SizeOnDisk.c_str(), Small );
                char age[48];
                std::snprintf( age, sizeof( age ), "measured %.0f s ago", ImGui::GetTime() - st.SizeMeasuredAt );
                // A number with no age is a number that will be wrong: this is a recursive walk of
                // the whole project, taken on demand and never per frame.
                TextAt( ImVec2( operations.x0 + 220, operations.y0 + 17 ), kTextFaint, age, Tiny );
            }
            if ( Button( ImVec2( operations.x1 - 48.0f, operations.y0 + 8 ), ICON_REFRESH, BtnKind::Solid, 34.0f,
                         28.0f ) )
            {
                st.SizeOnDisk     = MeasureSizeOnDisk( directory );
                st.SizeMeasuredAt = ImGui::GetTime();
            }

            const char* reveal      = ICON_OPEN_IN_NEW "  Reveal in Finder";
            const float revealWidth = TextW( reveal, Regular ) + 26.0f;
            if ( Button( ImVec2( operations.x0 + 16, operations.y0 + 50 ), reveal, BtnKind::Solid, revealWidth,
                         32.0f ) )
                RevealProject( st, entry );

            const char* remove      = ICON_TRASH "  Remove from list";
            const float removeWidth = TextW( remove, Regular ) + 26.0f;
            if ( Button( ImVec2( operations.x0 + 16 + revealWidth + 10.0f, operations.y0 + 50 ), remove,
                         BtnKind::Danger, removeWidth, 32.0f ) )
            {
                const int index = st.SettingsIndex;
                st.Screen       = View::Projects;
                RemoveEntry( st, index );
                return;
            }
            TextAt( ImVec2( operations.x0 + 26 + revealWidth + removeWidth + 14.0f, operations.y0 + 58 ),
                    kTextFaint, "files stay on disk", Small );
        }

        // ── the refusal, written down, where the next person will be tempted to build a tree ──
        y = operations.y1 + 22.0f;
        const Rect note{ c.x0, y, c.x1, std::min( c.y1, y + 62.0f ) };
        if ( note.H() > 30.0f )
        {
            Box( note, Fade( kInfo, 0.08f ), Fade( kInfo, 0.35f ), 5.0f );
            TextAt( ImVec2( note.x0 + 14, note.y0 + 12 ), kInfo, ICON_INFO, Small );
            Wrapped( ImVec2( note.x0 + 36, note.y0 + 10 ), kInfo,
                     "There is no Rendering, Physics or Input page here. Per-project state is three fields; "
                     "editor preferences are per USER (~/.desertengine/editor.json), scene settings are per "
                     "SCENE, and packaging lives in the Editor's Build Settings. A page for settings with no "
                     "consumer would be a TODO wearing the clothes of architecture.",
                     note.W() - 50.0f, Small );
        }
    }

    // ────────────────────────────────────────────────────────────── the graphics seam

    // THE ONLY graphics-API code outside the frame loop, and the reason nothing else in this
    // launcher names one. Replacing this function is the whole of what porting the thumbnails to
    // Vulkan (L3) costs.
    Hub::TextureBackend MakeTextureBackend()
    {
        Hub::TextureBackend backend;
        backend.Upload = []( const std::uint8_t* rgba, int width, int height ) -> Hub::TextureHandle
        {
            GLuint texture = 0;
            glGenTextures( 1, &texture );
            if ( texture == 0 )
                return nullptr;
            glBindTexture( GL_TEXTURE_2D, texture );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba );
            return reinterpret_cast<Hub::TextureHandle>( static_cast<std::uintptr_t>( texture ) );
        };
        backend.Destroy = []( Hub::TextureHandle handle )
        {
            const GLuint texture = static_cast<GLuint>( reinterpret_cast<std::uintptr_t>( handle ) );
            if ( texture )
                glDeleteTextures( 1, &texture );
        };
        return backend;
    }
} // namespace

int main()
{
    if ( !glfwInit() )
        return 1;

    // The design size, and the design minimum. At 860 wide the content column is 582 px, which the
    // column rule floors to exactly 2 — the rule's floor and the window's minimum agree rather than
    // one rescuing the other.
    GLFWwindow* window = glfwCreateWindow( 1280, 800, "DesertEngine Launcher", nullptr, nullptr );
    if ( !window )
    {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits( window, 860, 520, GLFW_DONT_CARE, GLFW_DONT_CARE );
    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    HubState st;
    st.Window = window;

    // Everything that has to be known before the first frame, in the order the answers depend on
    // each other: which engine, then its fonts, then its templates.
    {
        auto engines = Hub::LoadEngines( st.ConfigDirectory );
        if ( !engines.IsSuccess() )
            st.SetError( engines.GetError() );
        st.Engine = Hub::ChooseEngine( engines.IsSuccess() ? engines.GetValue() : Common::Engine::EngineRegistry{},
                                       std::getenv( "DESERT_ROOT" ) );
        if ( st.Engine.Root.empty() )
            st.SetError( st.Engine.Explanation );
    }
    Hub::Theme::LoadFonts( st.Engine.Root );
    Hub::Theme::ApplyStyle();
    st.TemplateScan = Hub::ScanTemplates( st.Engine.Root );

    {
        auto projects = Hub::LoadProjects( st.ConfigDirectory );
        if ( !projects.IsSuccess() )
            // The registry could not be read. Said out loud, on the strip, with the file named —
            // an empty grid with no explanation reads as "my projects vanished".
            st.SetError( projects.GetError() );
        else
            st.Registry = projects.ExtractValue();
    }

    ImGui_ImplGlfw_InitForOpenGL( window, true );
    ImGui_ImplOpenGL2_Init();
    st.Thumbnails.SetBackend( MakeTextureBackend() );

    if ( const char* home = std::getenv( "HOME" ) )
        std::snprintf( st.NewLocation, sizeof( st.NewLocation ), "%s/DesertProjects", home );

    // DESERT_CONFIG was a dead contract: both run scripts export it "for the hub" and nothing ever
    // read it — the picker always started on Release, so `RunProjectHub.sh Debug` launched a Release
    // Editor. Now it selects, and a value that is neither configuration name is REFUSED out loud
    // rather than quietly ignored.
    if ( const char* config = std::getenv( "DESERT_CONFIG" ); config && config[0] )
    {
        if ( std::strcmp( config, Common::Launch::kConfigDebug ) == 0 )
            st.Config = 0;
        else if ( std::strcmp( config, Common::Launch::kConfigRelease ) == 0 )
            st.Config = 1;
        else
            st.SetError( std::string( "DESERT_CONFIG=" ) + config + " is neither " + Common::Launch::kConfigDebug +
                         " nor " + Common::Launch::kConfigRelease + " - the picker is showing the default." );
    }

    while ( !glfwWindowShouldClose( window ) )
    {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::PushFont( Hub::Theme::Regular );
        st.ResolveEntriesIfStale();
        st.Thumbnails.BeginFrame();

        int width = 0, height = 0;
        glfwGetWindowSize( window, &width, &height );
        ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
        ImGui::SetNextWindowSize( ImVec2( static_cast<float>( width ), static_cast<float>( height ) ) );
        ImGui::Begin( "##hub", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar );

        const Frame frame = DrawFrame( st, static_cast<float>( width ), static_cast<float>( height ) );
        switch ( st.Screen )
        {
            case View::Projects:
                DrawProjectsView( st, frame );
                break;
            case View::NewProject:
                DrawNewProjectView( st, frame );
                break;
            case View::Settings:
                DrawSettingsView( st, frame );
                break;
        }
        DrawStatusStrip( st, frame.Status );

        ImGui::End();
        ImGui::PopFont();
        ImGui::Render();

        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize( window, &framebufferWidth, &framebufferHeight );
        glViewport( 0, 0, framebufferWidth, framebufferHeight );
        const ImVec4 clear = V4( Hub::Theme::kCanvas );
        glClearColor( clear.x, clear.y, clear.z, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT );
        ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );
        glfwSwapBuffers( window );
    }

    // Released while the GL context is still current. A cache destroyed at process exit would be
    // handing a dead context its textures to free.
    st.Thumbnails.Shutdown();

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow( window );
    glfwTerminate();
    return 0;
}
