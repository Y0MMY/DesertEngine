// Desert Project Hub — standalone launcher, fully separate from the Editor: it links NO engine
// code (R1) — only GLFW + ImGui + ReflectCpp, and compiles the shared serializer from the
// desert-shared submodule itself. UE/Unity-hub-style UI: sidebar navigation, project cards, a New
// Project flow.
//
//   * lists recent projects from ~/.desertengine/projects.json (same file the Editor maintains),
//     each resolved against the disk so a project that is gone or corrupt says so on its card
//   * creates new projects: folder structure + <Name>.deproj
//   * "Open" launches the Editor (Debug/Release pick in the sidebar) through an argv spawn
//
// Both file formats AND the content-folder layout come from the desert-shared submodule
// (<DesertShared/ProjectFormat.hpp>) — the same definition the engine reads them through, so what
// the hub writes is by construction what the Editor parses. (This file used to splice the JSON by
// hand next to a "keep the field names in sync" comment; a typo here produced a project the Editor
// silently refused to open.)
//
// This file is the WINDOW. Everything it does that could be wrong without a window on screen lives
// next door and is covered by Tests/Tools/ProjectHubContracts: Projects.hpp (registry entries,
// recent-list policy, name validation, project creation), Launch.hpp (the argv the Editor is
// started with), Files.hpp (the two file primitives), FileDialog.hpp (native panels).
//
// Run through scripts/MacOS/RunProjectHub.sh or scripts/Windows/RunProjectHub.bat — they export
// DESERT_ROOT / DESERT_CONFIG. Fonts load from $DESERT_ROOT/Editor/Resources/Fonts (falls back to
// the ImGui default when missing).
//
// STILL OPENGL2, and deliberately: R2 makes Vulkan the only supported backend, but porting the
// window is L2 stage E2 / L3 (§6.2) — it needs the shared VulkanWindow and the MoltenVK ICD
// environment, and doing it here would collide with the repository move. The four GL calls at the
// bottom of the frame loop are the whole of it.

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <GLFW/glfw3.h>

#include <DesertShared/LaunchProtocol.hpp>
#include <DesertShared/ProjectFormat.hpp>

#include "FileDialog.hpp"
#include "Files.hpp"
#include "Launch.hpp"
#include "Projects.hpp"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined( _WIN32 )
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Material Design icon literals (byte-identical to the editor's IconsMaterialDesignIcons.hpp).
#define HUB_ICON_PLUS "\xf3\xb0\x90\x95"
#define HUB_ICON_FOLDER_OPEN "\xf3\xb0\x9d\xb0"
#define HUB_ICON_DELETE "\xf3\xb0\x86\xb4"
#define HUB_ICON_ROCKET "\xf3\xb1\x93\x9e"
#define HUB_ICON_PACKAGE "\xf3\xb0\x8f\x96"
#define HUB_ICON_ALERT "\xf3\xb0\x80\xa3"
#define HUB_ICON_CHEVRON_LEFT "\xf3\xb0\x85\x81"

namespace
{
    namespace fs = std::filesystem;

    // The hub's OWN version (R1: no engine linkage, so Common::Version is gone — that was the last
    // thread). The version of the ENGINE a project will open with is a property of the engine
    // install, and will be read from ~/.desertengine/engines.json when that registry lands (L2 §7
    // item 4); until then the hub reports only itself. Bump by hand on user-visible changes.
    constexpr const char* kHubVersion = "0.1.0";

    // ------------------------------------------------------------------ persistence

    std::string ConfigDir()
    {
        const char* home = std::getenv( "HOME" );
#ifdef _WIN32
        if ( !home )
            home = std::getenv( "USERPROFILE" );
#endif
        fs::path dir = fs::path( home ? home : "." ) / ".desertengine";
        std::error_code ec;
        fs::create_directories( dir, ec );
        return dir.string();
    }

    std::string RegistryFile()
    {
        return ConfigDir() + "/projects.json";
    }

    // The registry is read and written through the SAME serializer the Editor uses (a hand-rolled
    // quoted-string scanner used to live here; it could not unescape a JSON string, so any Windows
    // path the Editor had written — backslashes escaped as `\\` — came back mangled).
    std::vector<std::string> LoadRecentProjects()
    {
        std::error_code ec;
        if ( !fs::exists( RegistryFile(), ec ) ) // no registry yet — a fresh machine, not an error
            return {};

        const auto raw = Hub::ReadTextFile( RegistryFile() );
        if ( !raw.IsSuccess() )
        {
            std::fprintf( stderr, "[Hub] %s\n", raw.GetError().c_str() );
            return {};
        }
        auto parsed = Common::Project::ReadProjectsRegistry( raw.GetValue() );
        if ( !parsed.IsSuccess() )
        {
            // The hub has no log window at startup; stderr with the reason beats an empty list that
            // reads as "my projects vanished".
            std::fprintf( stderr, "[Hub] %s: %s\n", RegistryFile().c_str(), parsed.GetError().c_str() );
            return {};
        }
        return parsed.ExtractValue().Projects;
    }

    // Returns the reason on failure — the file on disk then keeps its previous list, which for a
    // convenience file is the right failure: nothing is lost, the one change is. Callers decide
    // whether that is worth telling the user about; all of them now do.
    [[nodiscard]] Common::BoolResultStr SaveRecentProjects( const std::vector<std::string>& projects )
    {
        return Hub::WriteTextFile( RegistryFile(), Common::Project::WriteProjectsRegistry(
                                                        Common::Project::ProjectsRegistry{ projects } ) );
    }

    // ------------------------------------------------------------------ theme / fonts

    // Desert palette: near-black canvas, sand-orange accent.
    constexpr ImVec4 kBg       = ImVec4( 0.055f, 0.055f, 0.070f, 1.0f ); // #0E0E12
    constexpr ImVec4 kSidebar  = ImVec4( 0.075f, 0.075f, 0.095f, 1.0f );
    constexpr ImVec4 kPanel    = ImVec4( 0.100f, 0.100f, 0.125f, 1.0f );
    constexpr ImVec4 kPanelHov = ImVec4( 0.140f, 0.140f, 0.175f, 1.0f );
    constexpr ImVec4 kPanelDim = ImVec4( 0.080f, 0.078f, 0.090f, 1.0f ); // an entry that cannot open
    constexpr ImVec4 kAccent   = ImVec4( 0.910f, 0.530f, 0.170f, 1.0f ); // desert orange
    constexpr ImVec4 kAccentHi = ImVec4( 0.980f, 0.620f, 0.250f, 1.0f );
    constexpr ImVec4 kText     = ImVec4( 0.920f, 0.915f, 0.900f, 1.0f );
    constexpr ImVec4 kTextDim  = ImVec4( 0.560f, 0.560f, 0.620f, 1.0f );
    constexpr ImVec4 kBad      = ImVec4( 1.000f, 0.420f, 0.380f, 1.0f );

    ImFont* g_FontBody  = nullptr;
    ImFont* g_FontTitle = nullptr;
    ImFont* g_FontH1    = nullptr;

    void LoadFonts()
    {
        ImGuiIO& io = ImGui::GetIO();

        const char* root = std::getenv( "DESERT_ROOT" );
        const fs::path fontDir =
             fs::path( root ? root : "." ) / "Editor" / "Resources" / "Fonts";
        const fs::path body = fontDir / "Roboto-Regular.ttf";
        const fs::path bold = fontDir / "Roboto-Bold.ttf";
        const fs::path mdi  = fontDir / "materialdesignicons-webfont.ttf";

        if ( !fs::exists( body ) || !fs::exists( bold ) )
        {
            g_FontBody = g_FontTitle = g_FontH1 = io.Fonts->AddFontDefault();
            return;
        }

        static const ImWchar kIconRange[] = { 0xF0000, 0xF2000, 0 };

        auto addWithIcons = [&]( const fs::path& ttf, float size ) -> ImFont*
        {
            ImFont* font = io.Fonts->AddFontFromFileTTF( ttf.string().c_str(), size );
            if ( fs::exists( mdi ) )
            {
                ImFontConfig cfg;
                cfg.MergeMode  = true;
                cfg.GlyphOffset.y = 1.0f;
                io.Fonts->AddFontFromFileTTF( mdi.string().c_str(), size, &cfg, kIconRange );
            }
            return font;
        };

        g_FontBody  = addWithIcons( body, 17.0f );
        g_FontTitle = addWithIcons( bold, 20.0f );
        g_FontH1    = addWithIcons( bold, 30.0f );
    }

    void ApplyTheme()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 10.0f;
        style.FrameRounding     = 7.0f;
        style.PopupRounding     = 10.0f;
        style.GrabRounding      = 7.0f;
        style.WindowBorderSize  = 0.0f;
        style.ChildBorderSize   = 0.0f;
        style.FrameBorderSize   = 0.0f;
        style.FramePadding      = ImVec2( 12.0f, 8.0f );
        style.ItemSpacing       = ImVec2( 10.0f, 10.0f );
        style.WindowPadding     = ImVec2( 0.0f, 0.0f );
        style.ScrollbarSize     = 10.0f;
        style.ScrollbarRounding = 8.0f;

        ImVec4* c                     = style.Colors;
        c[ImGuiCol_WindowBg]          = kBg;
        c[ImGuiCol_ChildBg]           = ImVec4( 0, 0, 0, 0 );
        c[ImGuiCol_PopupBg]           = kPanel;
        c[ImGuiCol_Text]              = kText;
        c[ImGuiCol_TextDisabled]      = kTextDim;
        c[ImGuiCol_FrameBg]           = kPanel;
        c[ImGuiCol_FrameBgHovered]    = kPanelHov;
        c[ImGuiCol_FrameBgActive]     = kPanelHov;
        c[ImGuiCol_Button]            = kPanel;
        c[ImGuiCol_ButtonHovered]     = kPanelHov;
        c[ImGuiCol_ButtonActive]      = ImVec4( 0.18f, 0.18f, 0.23f, 1.0f );
        c[ImGuiCol_Header]            = kPanel;
        c[ImGuiCol_HeaderHovered]     = kPanelHov;
        c[ImGuiCol_HeaderActive]      = ImVec4( 0.18f, 0.18f, 0.23f, 1.0f );
        c[ImGuiCol_ScrollbarBg]       = ImVec4( 0, 0, 0, 0 );
        c[ImGuiCol_ScrollbarGrab]     = ImVec4( 0.25f, 0.25f, 0.30f, 1.0f );
        c[ImGuiCol_CheckMark]         = kAccent;
        c[ImGuiCol_SliderGrab]        = kAccent;
        c[ImGuiCol_Separator]         = ImVec4( 1, 1, 1, 0.06f );
    }

    // Accent-colored primary button.
    bool PrimaryButton( const char* label, const ImVec2& size = ImVec2( 0, 0 ) )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, kAccent );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, kAccentHi );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, kAccent );
        ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.08f, 0.06f, 0.03f, 1.0f ) );
        const bool pressed = ImGui::Button( label, size );
        ImGui::PopStyleColor( 4 );
        return pressed;
    }

    // Trims a path from the FRONT until it fits, because the identifying end of a path is its tail.
    // Card text used to be drawn unclipped and ran underneath the Open/Reveal/Remove buttons.
    std::string ElideFront( const std::string& text, float maxWidth )
    {
        if ( ImGui::CalcTextSize( text.c_str() ).x <= maxWidth )
            return text;
        // Three ASCII dots, not U+2026: the body font is loaded over the default (Latin-1) range
        // with only the icon block merged on top, so the real ellipsis draws as a missing-glyph box.
        const std::string ellipsis = "...";
        size_t            start    = 0;
        while ( start < text.size() &&
                ImGui::CalcTextSize( ( ellipsis + text.substr( start ) ).c_str() ).x > maxWidth )
            ++start;
        return ellipsis + text.substr( start );
    }

    // ------------------------------------------------------------------ app state

    enum class View
    {
        Projects,
        NewProject
    };

    struct HubState
    {
        std::vector<std::string>       Recent = LoadRecentProjects();
        std::vector<Hub::ProjectEntry> Entries;
        double                         EntriesResolvedAt = -1.0;
        View                           Screen            = View::Projects;
        int                            Config            = 1; // 0 = Debug, 1 = Release

        char        NewName[128]     = "MyGame";
        char        NewLocation[512] = "";
        int         Template         = 0; // index into Hub::Templates()
        std::string Status;
        bool        StatusIsError = false;

        GLFWwindow* Window = nullptr;

        // Re-resolves every registry line against the disk. Called on every mutation, and at most
        // once a second while the window is up: a project can be deleted, restored or repaired by
        // some other program while the hub sits open, and a card claiming otherwise is the defect
        // this whole state exists to close. It is ten small reads — measurably nothing.
        void ResolveEntries()
        {
            Entries           = Hub::ResolveProjectEntries( Recent );
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
    };

    // Writes the recent list and reports a failed write to the user. Used everywhere the list
    // changes, so no call site can forget that a registry write is an operation that can fail.
    void PersistRecent( HubState& st )
    {
        if ( const auto saved = SaveRecentProjects( st.Recent ); !saved.IsSuccess() )
            st.SetError( "Could not update " + RegistryFile() + ": " + saved.GetError() );
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

        const char* root = std::getenv( "DESERT_ROOT" );
        if ( !root )
        {
#ifdef _WIN32
            st.SetError( "DESERT_ROOT is not set - start the hub via scripts\\Windows\\RunProjectHub.bat" );
#else
            st.SetError( "DESERT_ROOT is not set - start the hub via scripts/MacOS/RunProjectHub.sh" );
#endif
            return;
        }

        const std::string config = st.Config == 0 ? Common::Launch::kConfigDebug : Common::Launch::kConfigRelease;
        const auto        launched = Hub::SpawnDetached( Hub::BuildEditorLaunch( root, config, entry.Path ) );
        if ( !launched.IsSuccess() )
        {
            // The old code discarded std::system()'s result and returned true no matter what, so a
            // launch that never happened still closed the window.
            st.SetError( "Could not start the Editor: " + launched.GetError() );
            return;
        }

        Hub::PromoteRecent( st.Recent, entry.Path );
        if ( const auto saved = SaveRecentProjects( st.Recent ); !saved.IsSuccess() )
        {
            // The Editor IS starting, so this must not read as a failed open — but the hub stays up
            // instead of closing over an error nobody would ever see.
            st.SetError( "The Editor is starting, but " + RegistryFile() +
                         " could not be updated: " + saved.GetError() );
            st.ResolveEntries();
            return;
        }
        glfwSetWindowShouldClose( st.Window, GLFW_TRUE ); // hub's job is done
    }

    void RevealProject( HubState& st, const Hub::ProjectEntry& entry )
    {
        if ( const auto revealed = Hub::SpawnDetached( Hub::BuildRevealCommand( entry.Path ) );
             !revealed.IsSuccess() )
            st.SetError( "Could not reveal " + entry.Path + ": " + revealed.GetError() );
    }

    // ------------------------------------------------------------------ views

    void DrawSidebar( HubState& st, float height )
    {
        ImGui::PushStyleColor( ImGuiCol_ChildBg, kSidebar );
        ImGui::BeginChild( "##sidebar", ImVec2( 230.0f, height ), false );

        ImGui::Dummy( ImVec2( 0, 18 ) );
        ImGui::PushFont( g_FontH1 );
        ImGui::SetCursorPosX( 24.0f );
        ImGui::TextColored( kAccent, "DESERT" );
        ImGui::PopFont();
        ImGui::PushFont( g_FontTitle );
        ImGui::SetCursorPosX( 24.0f );
        ImGui::TextColored( kTextDim, "ENGINE HUB" );
        ImGui::PopFont();
        ImGui::Dummy( ImVec2( 0, 22 ) );

        auto navItem = [&]( const char* icon, const char* label, View view )
        {
            const bool active = st.Screen == view;
            ImGui::SetCursorPosX( 12.0f );
            if ( active )
                ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.16f, 0.13f, 0.09f, 1.0f ) );
            ImGui::PushStyleColor( ImGuiCol_Text, active ? kAccent : kText );
            char buf[96];
            std::snprintf( buf, sizeof( buf ), " %s  %s", icon, label );
            if ( ImGui::Button( buf, ImVec2( 206.0f, 40.0f ) ) )
                st.Screen = view;
            ImGui::PopStyleColor( active ? 2 : 1 );
        };

        navItem( HUB_ICON_PACKAGE, "Projects", View::Projects );
        navItem( HUB_ICON_PLUS, "New Project", View::NewProject );

        // Bottom block: launch configuration + hint.
        ImGui::SetCursorPosY( height - 108.0f );
        ImGui::SetCursorPosX( 24.0f );
        ImGui::TextColored( kTextDim, "Editor build" );
        ImGui::SetCursorPosX( 24.0f );
        ImGui::SetNextItemWidth( 182.0f );
        // The labels ARE the config names the run scripts receive — one spelling, the protocol's.
        const char* configs[] = { Common::Launch::kConfigDebug, Common::Launch::kConfigRelease };
        ImGui::Combo( "##config", &st.Config, configs, 2 );

        ImGui::SetCursorPosX( 24.0f );
        ImGui::TextColored( kTextDim, "Hub %s", kHubVersion );

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Draws one card. Returns true when the user asked to forget this entry.
    bool DrawProjectCard( HubState& st, const Hub::ProjectEntry& entry, int index )
    {
        const bool  openable = entry.IsOpenable();
        const float height   = openable ? 76.0f : 96.0f;

        ImGui::PushID( index );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, openable ? kPanel : kPanelDim );
        ImGui::BeginChild( "##card", ImVec2( 0.0f, height ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        // Whole-card interaction: hover highlights, double-click opens. A card that cannot open
        // gets neither — an inert card is the honest shape of "there is nothing behind this".
        const ImVec2 cardMin = ImGui::GetWindowPos();
        const ImVec2 cardMax = ImVec2( cardMin.x + ImGui::GetWindowWidth(),
                                       cardMin.y + ImGui::GetWindowHeight() );
        const bool   hovered = openable && ImGui::IsMouseHoveringRect( cardMin, cardMax );
        if ( hovered )
            ImGui::GetWindowDrawList()->AddRectFilled( cardMin, cardMax,
                                                       ImGui::GetColorU32( kPanelHov ), 10.0f );
        if ( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            OpenProject( st, entry );

        // Icon
        ImGui::SetCursorPos( ImVec2( 18.0f, 22.0f ) );
        ImGui::PushFont( g_FontH1 );
        ImGui::TextColored( openable ? kAccent : kBad, openable ? HUB_ICON_PACKAGE : HUB_ICON_ALERT );
        ImGui::PopFont();

        const float right     = ImGui::GetWindowWidth();
        const float textWidth = right - 66.0f - 220.0f; // stop before the buttons

        // Name — from the .deproj the Editor will read, not from the path's stem.
        ImGui::SetCursorPos( ImVec2( 66.0f, 12.0f ) );
        ImGui::PushFont( g_FontTitle );
        ImGui::TextColored( openable ? kText : kTextDim, "%s", ElideFront( entry.Name, textWidth ).c_str() );
        ImGui::PopFont();

        ImGui::SetCursorPos( ImVec2( 66.0f, 42.0f ) );
        ImGui::TextColored( kTextDim, "%s", ElideFront( entry.Path, textWidth ).c_str() );

        if ( !openable )
        {
            ImGui::SetCursorPos( ImVec2( 66.0f, 66.0f ) );
            ImGui::TextColored( kBad, "%s", ElideFront( "Unavailable: " + entry.Trouble, textWidth ).c_str() );
        }

        // Right-side actions. Open is absent, not greyed: there is no version of this click that
        // does anything, and a button that only ever refuses is worse than no button.
        const float buttonY = ( height - 36.0f ) * 0.5f;
        if ( openable )
        {
            ImGui::SetCursorPos( ImVec2( right - 210.0f, buttonY ) );
            if ( PrimaryButton( HUB_ICON_ROCKET "  Open", ImVec2( 96.0f, 36.0f ) ) )
                OpenProject( st, entry );
        }

        std::error_code ec;
        if ( fs::exists( fs::path( entry.Path ).parent_path(), ec ) )
        {
            ImGui::SetCursorPos( ImVec2( right - 104.0f, buttonY ) );
            if ( ImGui::Button( HUB_ICON_FOLDER_OPEN, ImVec2( 40.0f, 36.0f ) ) )
                RevealProject( st, entry );
            if ( ImGui::IsItemHovered() )
#ifdef _WIN32
                ImGui::SetTooltip( "Show in Explorer" );
#else
                ImGui::SetTooltip( "Reveal in Finder" );
#endif
        }

        ImGui::SetCursorPos( ImVec2( right - 56.0f, buttonY ) );
        const bool removed = ImGui::Button( HUB_ICON_DELETE, ImVec2( 40.0f, 36.0f ) );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Remove from list (files stay on disk)" );

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        return removed;
    }

    void OpenExisting( HubState& st )
    {
        const std::string chosen =
             Hub::FileDialog::OpenFile( "Open existing project", "Desert project (.deproj)", "deproj" );
        if ( chosen.empty() ) // cancelled
            return;
        OpenProject( st, Hub::ResolveProjectEntry( chosen ) );
    }

    void DrawProjectsView( HubState& st )
    {
        // Header row
        ImGui::PushFont( g_FontH1 );
        ImGui::TextUnformatted( "Projects" );
        ImGui::PopFont();

        ImGui::SameLine( ImGui::GetContentRegionAvail().x - 300.0f );
        if ( ImGui::Button( HUB_ICON_FOLDER_OPEN "  Open existing", ImVec2( 150.0f, 38.0f ) ) )
            OpenExisting( st );
        ImGui::SameLine();
        if ( PrimaryButton( HUB_ICON_PLUS "  New Project", ImVec2( 140.0f, 38.0f ) ) )
            st.Screen = View::NewProject;

        ImGui::Dummy( ImVec2( 0, 6 ) );

        if ( st.Entries.empty() )
        {
            ImGui::Dummy( ImVec2( 0, 90 ) );
            ImGui::PushFont( g_FontTitle );
            const char*  msg = "No projects yet";
            const ImVec2 sz  = ImGui::CalcTextSize( msg );
            ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - sz.x ) * 0.5f );
            ImGui::TextColored( kTextDim, "%s", msg );
            ImGui::PopFont();
            const char*  hint = "Create one, or open an existing .deproj";
            const ImVec2 hs   = ImGui::CalcTextSize( hint );
            ImGui::SetCursorPosX( ( ImGui::GetContentRegionAvail().x - hs.x ) * 0.5f );
            ImGui::TextColored( kTextDim, "%s", hint );
            return;
        }

        ImGui::BeginChild( "##cards", ImVec2( 0, 0 ), false );
        // Collected, not applied in the loop: erasing while drawing shifts every card below the
        // one that was removed into a different index for the rest of the frame.
        int removeIndex = -1;
        for ( int i = 0; i < (int)st.Entries.size(); ++i )
            if ( DrawProjectCard( st, st.Entries[i], i ) )
                removeIndex = i;
        ImGui::EndChild();

        if ( removeIndex >= 0 && removeIndex < (int)st.Recent.size() )
        {
            st.Recent.erase( st.Recent.begin() + removeIndex );
            PersistRecent( st );
        }
    }

    void DrawNewProjectView( HubState& st )
    {
        if ( ImGui::Button( HUB_ICON_CHEVRON_LEFT "  Back", ImVec2( 92.0f, 34.0f ) ) )
            st.Screen = View::Projects;
        ImGui::Dummy( ImVec2( 0, 4 ) );

        ImGui::PushFont( g_FontH1 );
        ImGui::TextUnformatted( "New Project" );
        ImGui::PopFont();
        ImGui::Dummy( ImVec2( 0, 8 ) );

        // Template picker: one selectable card per starter template.
        ImGui::TextColored( kTextDim, "TEMPLATE" );
        const auto& templates = Hub::Templates();
        for ( int i = 0; i < static_cast<int>( templates.size() ); ++i )
        {
            const Hub::ProjectTemplate& tpl      = templates[i];
            const bool                  selected = ( st.Template == i );

            ImGui::PushID( tpl.Id );
            ImGui::PushStyleColor( ImGuiCol_ChildBg, selected ? kAccent : kPanel );
            ImGui::BeginChild( "##tplcard", ImVec2( 260.0f, 84.0f ), false );
            if ( ImGui::InvisibleButton( "##pick", ImVec2( 260.0f, 84.0f ) ) )
                st.Template = i;
            ImGui::SetCursorPos( ImVec2( 16, 12 ) );
            ImGui::PushFont( g_FontTitle );
            ImGui::TextColored( selected ? kPanel : kAccent, HUB_ICON_PACKAGE "  %s", tpl.Title );
            ImGui::PopFont();
            ImGui::SetCursorPos( ImVec2( 16, 44 ) );
            ImGui::PushTextWrapPos( 244.0f );
            ImGui::TextColored( selected ? kPanel : kTextDim, "%s", tpl.Description );
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();

            if ( ( i % 2 ) == 0 && i + 1 < static_cast<int>( templates.size() ) )
                ImGui::SameLine();
        }

        ImGui::Dummy( ImVec2( 0, 8 ) );
        ImGui::TextColored( kTextDim, "PROJECT NAME" );
        ImGui::SetNextItemWidth( 380.0f );
        ImGui::InputText( "##name", st.NewName, sizeof( st.NewName ) );
        // Said while typing, not after Create: the rule is the same one CreateProject enforces.
        if ( const std::string reason = Hub::ValidateProjectName( st.NewName ); !reason.empty() )
            ImGui::TextColored( kBad, "%s", reason.c_str() );

        ImGui::TextColored( kTextDim, "LOCATION" );
        ImGui::SetNextItemWidth( 380.0f );
        ImGui::InputText( "##loc", st.NewLocation, sizeof( st.NewLocation ) );
        ImGui::SameLine();
        if ( ImGui::Button( HUB_ICON_FOLDER_OPEN "  Browse", ImVec2( 130.0f, 38.0f ) ) )
        {
            const std::string chosen =
                 Hub::FileDialog::PickDirectory( "Where to create the project", st.NewLocation );
            if ( !chosen.empty() ) // "" is a cancel, and a cancel must not wipe the field
                std::snprintf( st.NewLocation, sizeof( st.NewLocation ), "%s", chosen.c_str() );
        }

        ImGui::Dummy( ImVec2( 0, 10 ) );
        if ( PrimaryButton( HUB_ICON_ROCKET "  Create & Open", ImVec2( 190.0f, 42.0f ) ) )
        {
            auto created = Hub::CreateProject( st.NewLocation, st.NewName, templates[st.Template] );
            if ( created.IsSuccess() )
                OpenProject( st, Hub::ResolveProjectEntry( created.ExtractValue() ) );
            else
                st.SetError( created.GetError() );
        }
    }
} // namespace

int main()
{
    if ( !glfwInit() )
        return 1;

    GLFWwindow* window = glfwCreateWindow( 1020, 640, "Desert Project Hub", nullptr, nullptr );
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
    LoadFonts();
    ApplyTheme();
    ImGui_ImplGlfw_InitForOpenGL( window, true );
    ImGui_ImplOpenGL2_Init();

    HubState st;
    st.Window = window;
    if ( const char* home = std::getenv( "HOME" ) )
        std::snprintf( st.NewLocation, sizeof( st.NewLocation ), "%s/DesertProjects", home );

    // DESERT_CONFIG was a dead contract: both run scripts export it "for the hub" and this file's
    // header comment said so, but nothing ever read it — the picker always started on Release, so
    // `RunProjectHub.sh Debug` launched a Release Editor. Now it selects, and a value that is
    // neither configuration name is REFUSED out loud rather than quietly ignored.
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
        ImGui::PushFont( g_FontBody );
        st.ResolveEntriesIfStale();

        int w, h;
        glfwGetWindowSize( window, &w, &h );
        ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
        ImGui::SetNextWindowSize( ImVec2( (float)w, (float)h ) );
        ImGui::Begin( "##hub", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar );

        DrawSidebar( st, (float)h );

        ImGui::SameLine( 0.0f, 0.0f );
        ImGui::BeginChild( "##main", ImVec2( 0, (float)h ), false );
        ImGui::SetCursorPos( ImVec2( 28.0f, 24.0f ) );
        ImGui::BeginGroup();
        {
            // Constrain the content column with right padding.
            ImGui::PushItemWidth( -28.0f );
            const float contentWidth = ImGui::GetContentRegionAvail().x - 28.0f;

            // The status line gets its space RESERVED before the views are drawn, instead of being
            // appended after them. It used to live inside the scrolling child at the end of the
            // project list, so the one moment it matters most — a launch that failed with fourteen
            // projects on screen — was the moment it sat below the fold, unread.
            const float statusHeight =
                 st.Status.empty()
                      ? 0.0f
                      : ImGui::CalcTextSize( st.Status.c_str(), nullptr, false, contentWidth ).y + 14.0f;

            ImGui::BeginChild( "##content",
                               ImVec2( contentWidth, ImGui::GetContentRegionAvail().y - statusHeight - 24.0f ),
                               false );

            if ( st.Screen == View::Projects )
                DrawProjectsView( st );
            else
                DrawNewProjectView( st );

            ImGui::EndChild();

            if ( !st.Status.empty() )
            {
                ImGui::PushTextWrapPos( ImGui::GetCursorPosX() + contentWidth );
                ImGui::TextColored( st.StatusIsError ? kBad : ImVec4( 0.5f, 0.9f, 0.5f, 1.0f ), "%s",
                                    st.Status.c_str() );
                ImGui::PopTextWrapPos();
            }
            ImGui::PopItemWidth();
        }
        ImGui::EndGroup();
        ImGui::EndChild();

        ImGui::End();
        ImGui::PopFont();
        ImGui::Render();

        glViewport( 0, 0, w, h );
        glClearColor( kBg.x, kBg.y, kBg.z, 1.0f );
        glClear( GL_COLOR_BUFFER_BIT );
        ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );
        glfwSwapBuffers( window );
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow( window );
    glfwTerminate();
    return 0;
}
