// Desert Project Hub — standalone launcher (separate from the Editor, links no engine code).
// UE/Unity-hub-style UI: sidebar navigation, project cards, a New Project flow.
//
//   * lists recent projects from ~/.desertengine/projects.json (same file the Editor maintains)
//   * creates new projects: folder structure + <Name>.deproj (JSON the Editor parses via reflect-cpp)
//   * "Open" launches the Editor (Debug/Release pick in the sidebar) via RunEditor.sh and exits
//
// Run through scripts/MacOS/RunProjectHub.sh — it exports DESERT_ROOT / DESERT_CONFIG. Fonts load
// from $DESERT_ROOT/Editor/Resources/Fonts (falls back to the ImGui default when missing).

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl2.h>

#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#elif defined( _WIN32 )
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Material Design icon literals (byte-identical to the editor's IconsMaterialDesignIcons.hpp).
#define HUB_ICON_PLUS "\xf3\xb0\x90\x95"
#define HUB_ICON_FOLDER_OPEN "\xf3\xb0\x9d\xb0"
#define HUB_ICON_DELETE "\xf3\xb0\x86\xb4"
#define HUB_ICON_ROCKET "\xf3\xb1\x93\x9e"
#define HUB_ICON_PACKAGE "\xf3\xb0\x8f\x96"
#define HUB_ICON_CHEVRON_LEFT "\xf3\xb0\x85\x81"

namespace
{
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------ persistence (unchanged logic)

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

    std::string ReadFile( const std::string& path )
    {
        std::ifstream f( path );
        if ( !f )
            return {};
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    void WriteFile( const std::string& path, const std::string& content )
    {
        std::ofstream f( path, std::ios::trunc );
        f << content;
    }

    // ~/.desertengine/projects.json has the trivial shape {"Projects":["...","..."]} — a tiny
    // quoted-string scanner keeps the hub dependency-free (the Editor writes it via reflect-cpp).
    std::vector<std::string> LoadRecentProjects()
    {
        const std::string        raw = ReadFile( RegistryFile() );
        std::vector<std::string> result;
        const size_t             open  = raw.find( '[' );
        const size_t             close = raw.rfind( ']' );
        if ( open == std::string::npos || close == std::string::npos || close < open )
            return result;

        size_t pos = open;
        while ( true )
        {
            const size_t q0 = raw.find( '"', pos );
            if ( q0 == std::string::npos || q0 > close )
                break;
            const size_t q1 = raw.find( '"', q0 + 1 );
            if ( q1 == std::string::npos || q1 > close )
                break;
            result.push_back( raw.substr( q0 + 1, q1 - q0 - 1 ) );
            pos = q1 + 1;
        }
        return result;
    }

    void SaveRecentProjects( const std::vector<std::string>& projects )
    {
        std::ostringstream ss;
        ss << "{\"Projects\":[";
        for ( size_t i = 0; i < projects.size(); ++i )
        {
            if ( i )
                ss << ',';
            ss << '"' << projects[i] << '"';
        }
        ss << "]}";
        WriteFile( RegistryFile(), ss.str() );
    }

    void RememberProject( std::vector<std::string>& recent, const std::string& deprojPath )
    {
        recent.erase( std::remove( recent.begin(), recent.end(), deprojPath ), recent.end() );
        recent.insert( recent.begin(), deprojPath );
        if ( recent.size() > 10 )
            recent.resize( 10 );
        SaveRecentProjects( recent );
    }

    std::string CreateProject( const std::string& parentDir, const std::string& name, std::string& error )
    {
        if ( name.empty() || parentDir.empty() )
        {
            error = "Project name and location are required.";
            return {};
        }

        const fs::path  root = fs::path( parentDir ) / name;
        std::error_code ec;
        if ( fs::exists( root ) && !fs::is_empty( root, ec ) )
        {
            error = "Folder already exists and is not empty: " + root.string();
            return {};
        }

        // Folder layout mirrors the engine's content constants (Common::Constants::Path).
        for ( const char* sub :
              { "Scenes", "Prefabs", "Scripts", "Textures", "Meshes", "Materials", "Collections" } )
            fs::create_directories( root / "Assets" / sub, ec );
        if ( ec )
        {
            error = "Could not create the project folders: " + ec.message();
            return {};
        }

        // Field names must match the Editor's ProjectFile struct (rfl::json parses this).
        const std::string  deproj = ( root / ( name + ".deproj" ) ).string();
        std::ostringstream ss;
        ss << "{\"Name\":\"" << name << "\",\"AssetsRoot\":\"Assets\",\"DefaultScene\":\"Assets/Scenes/"
           << name << ".desce\"}";
        WriteFile( deproj, ss.str() );

        error.clear();
        return deproj;
    }

    bool LaunchEditor( const std::string& deprojPath, const std::string& config, std::string& error )
    {
        const char* root = std::getenv( "DESERT_ROOT" );
        if ( !root )
        {
            error = "DESERT_ROOT is not set — start the hub via scripts/MacOS/RunProjectHub.sh";
            return false;
        }
#ifdef _WIN32
        error = "Windows launch wiring is not done yet — start the Editor manually with --project";
        return false;
#else
        std::ostringstream cmd;
        cmd << "cd \"" << root << "\" && ./scripts/MacOS/RunEditor.sh " << config << " --project \""
            << deprojPath << "\" >/dev/null 2>&1 &";
        std::system( cmd.str().c_str() );
        return true;
#endif
    }

    // ------------------------------------------------------------------ theme / fonts

    // Desert palette: near-black canvas, sand-orange accent.
    constexpr ImVec4 kBg       = ImVec4( 0.055f, 0.055f, 0.070f, 1.0f ); // #0E0E12
    constexpr ImVec4 kSidebar  = ImVec4( 0.075f, 0.075f, 0.095f, 1.0f );
    constexpr ImVec4 kPanel    = ImVec4( 0.100f, 0.100f, 0.125f, 1.0f );
    constexpr ImVec4 kPanelHov = ImVec4( 0.140f, 0.140f, 0.175f, 1.0f );
    constexpr ImVec4 kAccent   = ImVec4( 0.910f, 0.530f, 0.170f, 1.0f ); // desert orange
    constexpr ImVec4 kAccentHi = ImVec4( 0.980f, 0.620f, 0.250f, 1.0f );
    constexpr ImVec4 kText     = ImVec4( 0.920f, 0.915f, 0.900f, 1.0f );
    constexpr ImVec4 kTextDim  = ImVec4( 0.560f, 0.560f, 0.620f, 1.0f );

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

    // ------------------------------------------------------------------ app state

    enum class View
    {
        Projects,
        NewProject
    };

    struct HubState
    {
        std::vector<std::string> Recent = LoadRecentProjects();
        View                     Screen = View::Projects;
        int                      Config = 1; // 0 = Debug, 1 = Release

        char        NewName[128]     = "MyGame";
        char        NewLocation[512] = "";
        char        OpenPath[512]    = "";
        bool        OpenPopup        = false;
        std::string Status;
        bool        StatusIsError = false;

        GLFWwindow* Window = nullptr;
    };

    void OpenProject( HubState& st, const std::string& path )
    {
        if ( !fs::exists( path ) )
        {
            st.Status        = "File not found: " + path;
            st.StatusIsError = true;
            return;
        }
        std::string error;
        if ( LaunchEditor( path, st.Config == 0 ? "Debug" : "Release", error ) )
        {
            RememberProject( st.Recent, path );
            glfwSetWindowShouldClose( st.Window, GLFW_TRUE ); // hub's job is done
        }
        else
        {
            st.Status        = error;
            st.StatusIsError = true;
        }
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
        const char* configs[] = { "Debug", "Release" };
        ImGui::Combo( "##config", &st.Config, configs, 2 );

        ImGui::SetCursorPosX( 24.0f );
        ImGui::TextColored( kTextDim, "v0.1  |  dev" );

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawProjectCard( HubState& st, const std::string& path, int index )
    {
        const std::string name = fs::path( path ).stem().string();

        ImGui::PushID( index );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, kPanel );
        ImGui::BeginChild( "##card", ImVec2( 0.0f, 72.0f ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );

        // Whole-card interaction: click selects visual hover, double-click opens.
        const ImVec2 cardMin = ImGui::GetWindowPos();
        const ImVec2 cardMax = ImVec2( cardMin.x + ImGui::GetWindowWidth(),
                                       cardMin.y + ImGui::GetWindowHeight() );
        const bool hovered = ImGui::IsMouseHoveringRect( cardMin, cardMax );
        if ( hovered )
            ImGui::GetWindowDrawList()->AddRectFilled( cardMin, cardMax,
                                                       ImGui::GetColorU32( kPanelHov ), 10.0f );
        if ( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
            OpenProject( st, path );

        // Icon
        ImGui::SetCursorPos( ImVec2( 18.0f, 20.0f ) );
        ImGui::PushFont( g_FontH1 );
        ImGui::TextColored( kAccent, HUB_ICON_PACKAGE );
        ImGui::PopFont();

        // Name + path
        ImGui::SetCursorPos( ImVec2( 66.0f, 14.0f ) );
        ImGui::PushFont( g_FontTitle );
        ImGui::TextUnformatted( name.c_str() );
        ImGui::PopFont();
        ImGui::SetCursorPos( ImVec2( 66.0f, 40.0f ) );
        ImGui::TextColored( kTextDim, "%s", path.c_str() );

        // Right-side actions
        const float right = ImGui::GetWindowWidth();
        ImGui::SetCursorPos( ImVec2( right - 210.0f, 18.0f ) );
        if ( PrimaryButton( HUB_ICON_ROCKET "  Open", ImVec2( 96.0f, 36.0f ) ) )
            OpenProject( st, path );
        ImGui::SetCursorPos( ImVec2( right - 104.0f, 18.0f ) );
#ifdef __APPLE__
        if ( ImGui::Button( HUB_ICON_FOLDER_OPEN, ImVec2( 40.0f, 36.0f ) ) )
            std::system( ( "open \"" + fs::path( path ).parent_path().string() + "\"" ).c_str() );
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Reveal in Finder" );
#endif
        ImGui::SetCursorPos( ImVec2( right - 56.0f, 18.0f ) );
        bool removed = false;
        if ( ImGui::Button( HUB_ICON_DELETE, ImVec2( 40.0f, 36.0f ) ) )
            removed = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Remove from list (files stay on disk)" );

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();

        if ( removed )
        {
            st.Recent.erase( st.Recent.begin() + index );
            SaveRecentProjects( st.Recent );
        }
    }

    void DrawProjectsView( HubState& st )
    {
        // Header row
        ImGui::PushFont( g_FontH1 );
        ImGui::TextUnformatted( "Projects" );
        ImGui::PopFont();

        ImGui::SameLine( ImGui::GetContentRegionAvail().x - 300.0f );
        if ( ImGui::Button( HUB_ICON_FOLDER_OPEN "  Open existing", ImVec2( 150.0f, 38.0f ) ) )
            st.OpenPopup = true;
        ImGui::SameLine();
        if ( PrimaryButton( HUB_ICON_PLUS "  New Project", ImVec2( 140.0f, 38.0f ) ) )
            st.Screen = View::NewProject;

        ImGui::Dummy( ImVec2( 0, 6 ) );

        if ( st.Recent.empty() )
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
        for ( int i = 0; i < (int)st.Recent.size(); ++i )
            DrawProjectCard( st, st.Recent[i], i );
        ImGui::EndChild();
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

        // Template card (single template for now — mirrors the trimmed scope).
        ImGui::PushStyleColor( ImGuiCol_ChildBg, kPanel );
        ImGui::BeginChild( "##template", ImVec2( 260.0f, 96.0f ), false );
        ImGui::SetCursorPos( ImVec2( 16, 14 ) );
        ImGui::PushFont( g_FontTitle );
        ImGui::TextColored( kAccent, HUB_ICON_PACKAGE "  Empty Project" );
        ImGui::PopFont();
        ImGui::SetCursorPos( ImVec2( 16, 46 ) );
        ImGui::PushTextWrapPos( 244.0f );
        ImGui::TextColored( kTextDim, "Standard content folders + a default scene entry." );
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Dummy( ImVec2( 0, 8 ) );
        ImGui::TextColored( kTextDim, "PROJECT NAME" );
        ImGui::SetNextItemWidth( 380.0f );
        ImGui::InputText( "##name", st.NewName, sizeof( st.NewName ) );

        ImGui::TextColored( kTextDim, "LOCATION" );
        ImGui::SetNextItemWidth( 380.0f );
        ImGui::InputText( "##loc", st.NewLocation, sizeof( st.NewLocation ) );

        ImGui::Dummy( ImVec2( 0, 10 ) );
        if ( PrimaryButton( HUB_ICON_ROCKET "  Create & Open", ImVec2( 190.0f, 42.0f ) ) )
        {
            std::string error;
            if ( const std::string deproj = CreateProject( st.NewLocation, st.NewName, error );
                 !deproj.empty() )
                OpenProject( st, deproj );
            else
            {
                st.Status        = error;
                st.StatusIsError = true;
            }
        }
    }

    void DrawOpenPopup( HubState& st )
    {
        if ( st.OpenPopup )
        {
            ImGui::OpenPopup( "Open existing project" );
            st.OpenPopup = false;
        }
        ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                 ImVec2( 0.5f, 0.5f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 18, 18 ) );
        if ( ImGui::BeginPopupModal( "Open existing project", nullptr,
                                     ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextColored( kTextDim, "PATH TO .DEPROJ" );
            ImGui::SetNextItemWidth( 480.0f );
            ImGui::InputText( "##openPath", st.OpenPath, sizeof( st.OpenPath ) );
            ImGui::Dummy( ImVec2( 0, 4 ) );
            if ( PrimaryButton( "Open", ImVec2( 110.0f, 36.0f ) ) && st.OpenPath[0] )
            {
                OpenProject( st, st.OpenPath );
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel", ImVec2( 96.0f, 36.0f ) ) )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
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

    while ( !glfwWindowShouldClose( window ) )
    {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::PushFont( g_FontBody );

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
            ImGui::BeginChild( "##content", ImVec2( ImGui::GetContentRegionAvail().x - 28.0f, 0 ),
                               false );

            if ( st.Screen == View::Projects )
                DrawProjectsView( st );
            else
                DrawNewProjectView( st );

            // Status line (errors from create/open).
            if ( !st.Status.empty() )
            {
                ImGui::Dummy( ImVec2( 0, 6 ) );
                ImGui::PushTextWrapPos( 0.0f );
                ImGui::TextColored( st.StatusIsError ? ImVec4( 1.0f, 0.42f, 0.38f, 1.0f )
                                                     : ImVec4( 0.5f, 0.9f, 0.5f, 1.0f ),
                                    "%s", st.Status.c_str() );
                ImGui::PopTextWrapPos();
            }

            ImGui::EndChild();
            ImGui::PopItemWidth();
        }
        ImGui::EndGroup();
        ImGui::EndChild();

        DrawOpenPopup( st );

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
