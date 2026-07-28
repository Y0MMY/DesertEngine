// Desert Project Hub — standalone launcher (separate from the Editor, links no engine code).
//
//   * lists recent projects from ~/.desertengine/projects.json (same file the Editor maintains)
//   * creates new projects: folder structure + <Name>.deproj (JSON the Editor parses with reflect-cpp)
//   * "Open" launches the Editor via scripts/MacOS/RunEditor.sh <Config> --project <path> and exits
//
// Run through scripts/MacOS/RunProjectHub.sh — it exports DESERT_ROOT / DESERT_CONFIG, which the hub
// needs to find the launch script.

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

namespace
{
    namespace fs = std::filesystem;

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

    // ~/.desertengine/projects.json has the trivial shape {"Projects":["...","..."]} (written by both
    // the hub and the Editor's reflect-cpp side). A tiny quoted-string scanner is enough to read it —
    // no JSON library in the hub keeps it dependency-free.
    std::vector<std::string> LoadRecentProjects()
    {
        const std::string        raw = ReadFile( RegistryFile() );
        std::vector<std::string> result;
        const size_t             open = raw.find( '[' );
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

    // Creates <parentDir>/<name>/ with the standard folder layout and a .deproj the Editor can parse.
    // Returns the .deproj path ("" on failure).
    std::string CreateProject( const std::string& parentDir, const std::string& name, std::string& error )
    {
        if ( name.empty() || parentDir.empty() )
        {
            error = "Project name and location are required.";
            return {};
        }

        const fs::path root = fs::path( parentDir ) / name;
        std::error_code ec;
        if ( fs::exists( root ) && !fs::is_empty( root, ec ) )
        {
            error = "Folder already exists and is not empty: " + root.string();
            return {};
        }

        // Folder layout mirrors the engine's content constants (Common::Constants::Path).
        for ( const char* sub : { "Scenes", "Prefabs", "Scripts", "Textures", "Meshes", "Materials",
                                  "Collections" } )
            fs::create_directories( root / "Assets" / sub, ec );
        if ( ec )
        {
            error = "Could not create the project folders: " + ec.message();
            return {};
        }

        // Field names must match the Editor's ProjectFile struct (rfl::json parses this).
        const std::string deproj = ( root / ( name + ".deproj" ) ).string();
        std::ostringstream ss;
        ss << "{\"Name\":\"" << name << "\",\"AssetsRoot\":\"Assets\",\"DefaultScene\":\"Assets/Scenes/"
           << name << ".desce\"}";
        WriteFile( deproj, ss.str() );

        error.clear();
        return deproj;
    }

    // Launch the Editor with the project and close the hub (Unity Hub behavior).
    bool LaunchEditor( const std::string& deprojPath, std::string& error )
    {
        const char* root   = std::getenv( "DESERT_ROOT" );
        const char* config = std::getenv( "DESERT_CONFIG" );
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
        cmd << "cd \"" << root << "\" && ./scripts/MacOS/RunEditor.sh " << ( config ? config : "Debug" )
            << " --project \"" << deprojPath << "\" >/dev/null 2>&1 &";
        std::system( cmd.str().c_str() );
        return true;
#endif
    }
} // namespace

int main()
{
    if ( !glfwInit() )
        return 1;

    glfwWindowHint( GLFW_RESIZABLE, GLFW_FALSE );
    GLFWwindow* window = glfwCreateWindow( 640, 480, "Desert Project Hub", nullptr, nullptr );
    if ( !window )
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui_ImplGlfw_InitForOpenGL( window, true );
    ImGui_ImplOpenGL2_Init();

    std::vector<std::string> recent = LoadRecentProjects();

    char        newName[128]     = "MyGame";
    char        newLocation[512] = "";
    char        openPath[512]    = "";
    std::string status;
    bool        statusIsError = false;
    int         selected      = -1;

    if ( const char* home = std::getenv( "HOME" ) )
        std::snprintf( newLocation, sizeof( newLocation ), "%s/DesertProjects", home );

    auto openProject = [&]( const std::string& path )
    {
        std::string error;
        if ( !std::filesystem::exists( path ) )
        {
            status        = "File not found: " + path;
            statusIsError = true;
            return;
        }
        if ( LaunchEditor( path, error ) )
        {
            RememberProject( recent, path );
            glfwSetWindowShouldClose( window, GLFW_TRUE ); // hub's job is done
        }
        else
        {
            status        = error;
            statusIsError = true;
        }
    };

    while ( !glfwWindowShouldClose( window ) )
    {
        glfwPollEvents();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int w, h;
        glfwGetWindowSize( window, &w, &h );
        ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
        ImGui::SetNextWindowSize( ImVec2( (float)w, (float)h ) );
        ImGui::Begin( "##hub", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoBringToFrontOnFocus );

        ImGui::TextUnformatted( "DESERT PROJECT HUB" );
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Recent projects ----
        ImGui::TextDisabled( "Recent projects" );
        ImGui::BeginChild( "##recent", ImVec2( 0, 180 ), true );
        if ( recent.empty() )
            ImGui::TextDisabled( "No projects yet — create one below." );
        for ( int i = 0; i < (int)recent.size(); ++i )
        {
            const std::string name = std::filesystem::path( recent[i] ).stem().string();
            if ( ImGui::Selectable( ( name + "##" + std::to_string( i ) ).c_str(), selected == i,
                                    ImGuiSelectableFlags_AllowDoubleClick ) )
            {
                selected = i;
                if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                    openProject( recent[i] );
            }
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", recent[i].c_str() );
        }
        ImGui::EndChild();

        if ( ImGui::Button( "Open selected", ImVec2( 140, 0 ) ) && selected >= 0 &&
             selected < (int)recent.size() )
            openProject( recent[selected] );
        ImGui::SameLine();
        if ( ImGui::Button( "Remove from list", ImVec2( 140, 0 ) ) && selected >= 0 &&
             selected < (int)recent.size() )
        {
            recent.erase( recent.begin() + selected );
            SaveRecentProjects( recent );
            selected = -1;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Create new ----
        ImGui::TextDisabled( "Create a new project" );
        ImGui::SetNextItemWidth( 200 );
        ImGui::InputText( "Name", newName, sizeof( newName ) );
        ImGui::SetNextItemWidth( 420 );
        ImGui::InputText( "Location", newLocation, sizeof( newLocation ) );
        if ( ImGui::Button( "Create & Open", ImVec2( 140, 0 ) ) )
        {
            std::string error;
            if ( const std::string deproj = CreateProject( newLocation, newName, error ); !deproj.empty() )
                openProject( deproj );
            else
            {
                status        = error;
                statusIsError = true;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Open existing ----
        ImGui::TextDisabled( "Open an existing .deproj" );
        ImGui::SetNextItemWidth( 420 );
        ImGui::InputText( "##openPath", openPath, sizeof( openPath ) );
        ImGui::SameLine();
        if ( ImGui::Button( "Open", ImVec2( 80, 0 ) ) && openPath[0] )
            openProject( openPath );

        if ( !status.empty() )
        {
            ImGui::Spacing();
            ImGui::TextColored( statusIsError ? ImVec4( 1.0f, 0.4f, 0.4f, 1.0f )
                                              : ImVec4( 0.5f, 0.9f, 0.5f, 1.0f ),
                                "%s", status.c_str() );
        }

        ImGui::End();
        ImGui::Render();

        glViewport( 0, 0, w, h );
        glClearColor( 0.09f, 0.09f, 0.11f, 1.0f );
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
