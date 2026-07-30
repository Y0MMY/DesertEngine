#include "PhotogrammetryPanel.hpp"

#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Import/MeshDnD.hpp>
#include <Editor/Widgets/ThumbnailCache.hpp>
#include <Editor/Widgets/UIHelper/ImGuiUI.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

#include <Common/Core/Logger.hpp>

#include <ImGui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <system_error>

#if defined( __APPLE__ ) || defined( __unix__ )
#define DE_POSIX_PROC 1
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // House palette (matches MeshEditorPanel) so the panel reads as part of the editor, not a bolt-on.
        constexpr ImVec4 kColHeader    = { 0.13f, 0.14f, 0.17f, 1.0f };
        constexpr ImVec4 kColSection   = { 0.11f, 0.12f, 0.14f, 1.0f };
        constexpr ImVec4 kColLogBg     = { 0.07f, 0.07f, 0.08f, 1.0f };
        constexpr ImVec4 kColAccent    = { 0.20f, 0.44f, 0.72f, 1.0f };
        constexpr ImVec4 kColAccentHov = { 0.26f, 0.52f, 0.82f, 1.0f };
        constexpr ImVec4 kColDanger    = { 0.62f, 0.22f, 0.22f, 1.0f };
        constexpr ImVec4 kColOk        = { 0.55f, 0.85f, 0.55f, 1.0f };
        constexpr ImVec4 kColWarn      = { 0.95f, 0.75f, 0.35f, 1.0f };
        constexpr ImVec4 kColErr       = { 0.95f, 0.45f, 0.45f, 1.0f };

        constexpr std::size_t kMaxLogLines = 600;

        struct CmdPreset
        {
            const char* name;
            const char* cmd;
        };

        // Reconstruct presets, split by mode. "Custom" is implicit (an edited command matches nothing).
        constexpr CmdPreset kReconstructObject[] = {
             { "Meshroom (AliceVision)", "meshroom_batch --input {input} --output {outdir}" },
             { "COLMAP (automatic)",
               "colmap automatic_reconstructor --workspace_path {outdir} --image_path {input}" },
             { "RealityCapture (CLI)",
               "RealityCapture -addFolder {input} -align -setReconstructionRegionAuto -calculateModel "
               "-exportModel {output} -quit" },
        };
        constexpr CmdPreset kReconstructFace[] = {
             { "Meshroom (head, high detail)", "meshroom_batch --input {input} --output {outdir}" },
             { "Face solver (your CLI)", "your_face_tool --frames {input} --out {output}" },
        };
        constexpr CmdPreset kCaptureObject[] = {
             { "Webcam -> image burst (ffmpeg)",
               "ffmpeg -y -f avfoundation -framerate 2 -i 0 -t 20 -q:v 2 {photos}/frame_%03d.jpg" },
             { "Webcam -> 1 fps (ffmpeg)",
               "ffmpeg -y -f avfoundation -framerate 1 -i 0 -t 30 -q:v 2 {photos}/frame_%03d.jpg" },
        };
        constexpr CmdPreset kCaptureFace[] = {
             { "Webcam sweep 30s (ffmpeg)",
               "ffmpeg -y -f avfoundation -framerate 3 -i 0 -t 30 -q:v 2 {photos}/frame_%03d.jpg" },
        };

        bool IsImageExt( const std::filesystem::path& p )
        {
            std::string ext = p.extension().string();
            for ( auto& c : ext )
                c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tga" || ext == ".bmp";
        }

        // Substitutes {input}/{output}/{outdir} in the command template.
        std::string Substitute( std::string cmd, const std::string& input, const std::string& output )
        {
            const std::string outdir  = std::filesystem::path( output ).parent_path().string();
            const auto        replace = [&]( const char* token, const std::string& value )
            {
                for ( size_t p = cmd.find( token ); p != std::string::npos; p = cmd.find( token, p ) )
                    cmd.replace( p, std::strlen( token ), value );
            };
            replace( "{input}", input );
            replace( "{output}", output );
            replace( "{outdir}", outdir );
            return cmd;
        }

        // Multi-line command editor backed by a std::string (copied in/out around the widget).
        bool CommandField( const char* id, std::string& value )
        {
            char buf[2048];
            std::snprintf( buf, sizeof( buf ), "%s", value.c_str() );
            const ImVec2 sz( -1.0f, ImGui::GetTextLineHeight() * 2.2f );
            const bool   changed = ImGui::InputTextMultiline( id, buf, sizeof( buf ), sz );
            if ( changed )
                value = buf;
            return changed;
        }

        // Preset dropdown: the current label is the matching preset name or "Custom". Picking one writes it
        // into `value`. Returns true when the value changed.
        bool PresetCombo( const char* id, const CmdPreset* presets, int count, std::string& value )
        {
            const char* current = "Custom";
            for ( int i = 0; i < count; ++i )
            {
                if ( value == presets[i].cmd )
                {
                    current = presets[i].name;
                    break;
                }
            }

            bool changed = false;
            ImGui::SetNextItemWidth( -1.0f );
            if ( ImGui::BeginCombo( id, current ) )
            {
                for ( int i = 0; i < count; ++i )
                {
                    const bool sel = ( value == presets[i].cmd );
                    if ( ImGui::Selectable( presets[i].name, sel ) )
                    {
                        value   = presets[i].cmd;
                        changed = true;
                    }
                    if ( sel )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::Separator();
                ImGui::TextDisabled( "Edit the field below for a Custom command." );
                ImGui::EndCombo();
            }
            return changed;
        }

        // A path field with a native "Browse…" button: [ editable field ][ Browse ]. `folder` picks a
        // directory (OpenFolderDialog); otherwise a save-file dialog (the output mesh).
        bool PathPicker( const char* id, std::string& value, bool folder )
        {
            constexpr float btnW = 92.0f;
            ImGui::PushID( id );

            char buf[1024];
            std::snprintf( buf, sizeof( buf ), "%s", value.c_str() );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - btnW - 8.0f );
            bool changed = ImGui::InputText( "##field", buf, sizeof( buf ) );
            if ( changed )
                value = buf;

            ImGui::SameLine();
            if ( ImGui::Button( ICON_MDI_FOLDER_OPEN " Browse", ImVec2( btnW, 0.0f ) ) )
            {
                std::filesystem::path picked;
                if ( folder )
                    picked = Common::Utils::FileSystem::OpenFolderDialog();
                else
                    picked = Common::Utils::FileSystem::SaveFileDialog(
                         "Meshes\0*.obj;*.glb;*.gltf;*.fbx;*.ply\0All\0*.*\0" );
                if ( !picked.empty() )
                {
                    value   = picked.string();
                    changed = true;
                }
            }

            ImGui::PopID();
            return changed;
        }
    } // namespace

    PhotogrammetryPanel::PhotogrammetryPanel( const std::shared_ptr<::Desert::Core::Scene>& scene,
                                              Assets::AssetManager*                         assets )
         : IPanel( "Model from Photos", /*showPanel=*/false ), m_Scene( scene ), m_Assets( assets )
    {
        m_UIHelper = std::make_unique<UI::UIHelper>();
        m_UIHelper->Init();
        m_Thumbnails = std::make_unique<ThumbnailCache>();
        m_Mode       = static_cast<Mode>( EditorPreferences::Get().PhotogrammetryMode );
    }

    PhotogrammetryPanel::~PhotogrammetryPanel()
    {
        CancelJob();
        if ( m_Worker.joinable() )
            m_Worker.join();
    }

    void PhotogrammetryPanel::PushLog( const std::string& line )
    {
        std::lock_guard<std::mutex> lk( m_LogMutex );
        if ( m_Log.size() >= kMaxLogLines )
            m_Log.erase( m_Log.begin(), m_Log.begin() + ( m_Log.size() - kMaxLogLines + 1 ) );
        m_Log.push_back( line );
    }

    void PhotogrammetryPanel::RunCommand( const std::string& cmd, Job job )
    {
        LOG_INFO( "[Photogrammetry] running: {}", cmd );
        m_Job         = job;
        m_StatusError = false;
        m_Running     = true;
        m_Done        = false;
        {
            std::lock_guard<std::mutex> lk( m_LogMutex );
            m_Log.clear();
            m_Log.push_back( "$ " + cmd );
        }

        if ( m_Worker.joinable() )
            m_Worker.join();

        m_Worker = std::thread(
             [this, cmd]()
             {
#if defined( DE_POSIX_PROC )
                 int fds[2];
                 if ( pipe( fds ) != 0 )
                 {
                     m_ExitCode = -1;
                     m_Running  = false;
                     m_Done     = true;
                     return;
                 }

                 const pid_t pid = fork();
                 if ( pid == 0 )
                 {
                     // Child: own process group (so Cancel can signal the whole tree), stdout+stderr -> pipe.
                     setpgid( 0, 0 );
                     dup2( fds[1], STDOUT_FILENO );
                     dup2( fds[1], STDERR_FILENO );
                     close( fds[0] );
                     close( fds[1] );
                     execl( "/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>( nullptr ) );
                     _exit( 127 );
                 }

                 close( fds[1] );
                 m_ChildPid = pid;

                 // Stream stdout line by line into the shared log (blocking reads on this worker thread).
                 std::string acc;
                 char        buf[512];
                 ssize_t     n;
                 while ( ( n = read( fds[0], buf, sizeof( buf ) ) ) > 0 )
                 {
                     acc.append( buf, static_cast<size_t>( n ) );
                     size_t nl;
                     while ( ( nl = acc.find( '\n' ) ) != std::string::npos )
                     {
                         PushLog( acc.substr( 0, nl ) );
                         acc.erase( 0, nl + 1 );
                     }
                 }
                 if ( !acc.empty() )
                     PushLog( acc );
                 close( fds[0] );

                 int status = 0;
                 waitpid( pid, &status, 0 );
                 m_ChildPid = -1;
                 if ( WIFEXITED( status ) )
                     m_ExitCode = WEXITSTATUS( status );
                 else if ( WIFSIGNALED( status ) )
                     m_ExitCode = 130; // treated as "cancelled"
                 else
                     m_ExitCode = -1;
#else
                 // Fallback (no streaming / cancel): run through the shell and capture only the exit code.
                 const int rc = std::system( cmd.c_str() );
                 m_ExitCode   = rc;
#endif
                 m_Running = false;
                 m_Done    = true;
             } );
    }

    void PhotogrammetryPanel::CancelJob()
    {
#if defined( DE_POSIX_PROC )
        const long pid = m_ChildPid.load();
        if ( pid > 0 )
        {
            kill( static_cast<pid_t>( -pid ), SIGTERM ); // signal the whole process group
            PushLog( "[cancelled by user]" );
        }
#endif
    }

    void PhotogrammetryPanel::StartCapture()
    {
        auto& prefs = EditorPreferences::Get();
        if ( prefs.PhotogrammetryPhotosDir.empty() )
        {
            m_Status      = "Set the photos folder first.";
            m_StatusError = true;
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories( prefs.PhotogrammetryPhotosDir, ec );

        std::string cmd = prefs.PhotogrammetryCaptureCommand;
        for ( size_t p = cmd.find( "{photos}" ); p != std::string::npos; p = cmd.find( "{photos}", p ) )
            cmd.replace( p, 8, prefs.PhotogrammetryPhotosDir );

        m_Status = "Capturing frames from the camera… move slowly around the subject.";
        RunCommand( cmd, Job::Capture );
    }

    void PhotogrammetryPanel::StartReconstruction()
    {
        auto& prefs = EditorPreferences::Get();
        if ( prefs.PhotogrammetryPhotosDir.empty() )
        {
            m_Status      = "Set the photos folder first.";
            m_StatusError = true;
            return;
        }

        m_OutputCaptured = prefs.PhotogrammetryOutputMesh;
        std::error_code ec;
        std::filesystem::create_directories( std::filesystem::path( m_OutputCaptured ).parent_path(), ec );

        const std::string cmd =
             Substitute( prefs.PhotogrammetryCommand, prefs.PhotogrammetryPhotosDir, m_OutputCaptured );

        m_Status = "Running the external tool… (this can take minutes)";
        RunCommand( cmd, Job::Reconstruct );
    }

    void PhotogrammetryPanel::ImportResult()
    {
        std::error_code ec;
        if ( m_ExitCode != 0 )
        {
            m_Status      = "The external tool exited with code " + std::to_string( m_ExitCode.load() ) + ".";
            m_StatusError = true;
            return;
        }
        if ( m_OutputCaptured.empty() || !std::filesystem::exists( m_OutputCaptured, ec ) )
        {
            m_Status      = "Tool finished but no mesh at '" + m_OutputCaptured + "'. Check {output} vs the tool.";
            m_StatusError = true;
            return;
        }
        if ( !m_Assets || !m_Scene )
        {
            m_Status      = "No scene / asset manager to import into.";
            m_StatusError = true;
            return;
        }

        auto&      mgr      = const_cast<Assets::AssetManager&>( *m_Assets );
        const auto resolved = MeshDnD::ResolveOrImportMesh( mgr, m_OutputCaptured );
        if ( resolved.Handle.IsNull() )
        {
            m_Status      = "Reconstruction succeeded but the mesh failed to import.";
            m_StatusError = true;
            return;
        }

        auto& e =
             m_Scene->CreateNewEntity( m_Mode == Mode::Face ? "Photogrammetry Head" : "Photogrammetry Model" );
        if ( resolved.Skinned )
        {
            e.AddComponent<ECS::SkinnedMeshComponent>();
            e.GetComponent<ECS::SkinnedMeshComponent>().MeshHandle = resolved.Handle;
            e.AddComponent<ECS::AnimationComponent>();
        }
        else
        {
            e.AddComponent<ECS::StaticMeshComponent>();
            e.GetComponent<ECS::StaticMeshComponent>().MeshHandle = resolved.Handle;
        }
        m_Status      = "Imported the reconstructed mesh and spawned it into the scene.";
        m_StatusError = false;
    }

    void PhotogrammetryPanel::Reimport()
    {
        if ( m_OutputCaptured.empty() )
            m_OutputCaptured = EditorPreferences::Get().PhotogrammetryOutputMesh;
        m_ExitCode = 0; // Reimport is a manual action on an existing file; don't gate on the last run.
        ImportResult();
    }

    void PhotogrammetryPanel::OpenOutputFolder()
    {
        std::string out = m_OutputCaptured;
        if ( out.empty() )
            out = EditorPreferences::Get().PhotogrammetryOutputMesh;
        std::error_code   ec;
        const std::string dir = std::filesystem::absolute( out, ec ).parent_path().string();
        if ( dir.empty() )
            return;
#if defined( __APPLE__ )
        std::system( ( "open \"" + dir + "\"" ).c_str() );
#elif defined( __unix__ )
        std::system( ( "xdg-open \"" + dir + "\" &" ).c_str() );
#elif defined( _WIN32 )
        std::system( ( "explorer \"" + dir + "\"" ).c_str() );
#endif
    }

    void PhotogrammetryPanel::RescanPhotos()
    {
        auto& prefs   = EditorPreferences::Get();
        m_ScannedDir  = prefs.PhotogrammetryPhotosDir;
        m_PhotosDirty = false;
        m_PhotoFiles.clear();
        m_SelectedPhoto = -1;
        if ( m_ScannedDir.empty() )
            return;
        std::error_code ec;
        if ( !std::filesystem::is_directory( m_ScannedDir, ec ) )
            return;
        for ( const auto& f : std::filesystem::directory_iterator( m_ScannedDir, ec ) )
        {
            if ( f.is_regular_file( ec ) && IsImageExt( f.path() ) )
                m_PhotoFiles.push_back( f.path().string() );
        }
        std::sort( m_PhotoFiles.begin(), m_PhotoFiles.end() );
    }

    // ---------------------------------------------------------------------------------------------------

    void PhotogrammetryPanel::DrawToolbar( bool running )
    {
        auto& prefs = EditorPreferences::Get();

        ImGui::PushStyleColor( ImGuiCol_ChildBg, kColHeader );
        ImGui::BeginChild( "##pgToolbar", ImVec2( 0.0f, 44.0f ), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        ImGui::SetCursorPosY( 8.0f );
        ImGui::SetCursorPosX( 8.0f );

        const bool hasFolder = !prefs.PhotogrammetryPhotosDir.empty();

        // Capture
        ImGui::BeginDisabled( running || !hasFolder );
        if ( ImGui::Button( ICON_MDI_CAMERA "  Capture", ImVec2( 130.0f, 28.0f ) ) )
            StartCapture();
        ImGui::EndDisabled();
        ImGui::SameLine( 0.0f, 6.0f );

        // Reconstruct (primary)
        ImGui::BeginDisabled( running || !hasFolder );
        ImGui::PushStyleColor( ImGuiCol_Button, kColAccent );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, kColAccentHov );
        if ( ImGui::Button( ICON_MDI_CUBE_SCAN "  Reconstruct", ImVec2( 150.0f, 28.0f ) ) )
            StartReconstruction();
        ImGui::PopStyleColor( 2 );
        ImGui::EndDisabled();
        ImGui::SameLine( 0.0f, 6.0f );

        // Cancel
        ImGui::BeginDisabled( !running );
        ImGui::PushStyleColor( ImGuiCol_Button, kColDanger );
        if ( ImGui::Button( ICON_MDI_STOP "  Cancel", ImVec2( 110.0f, 28.0f ) ) )
            CancelJob();
        ImGui::PopStyleColor();
        ImGui::EndDisabled();

        // Right-aligned: Reimport + Open output folder
        const float rightW = 130.0f + 150.0f + 12.0f;
        ImGui::SameLine();
        ImGui::SetCursorPosX( ImGui::GetWindowContentRegionMax().x - rightW );

        std::error_code ec;
        const bool      hasOutput = std::filesystem::exists(
             m_OutputCaptured.empty() ? prefs.PhotogrammetryOutputMesh : m_OutputCaptured, ec );
        ImGui::BeginDisabled( running || !hasOutput );
        if ( ImGui::Button( ICON_MDI_IMPORT "  Import result", ImVec2( 130.0f, 28.0f ) ) )
            Reimport();
        ImGui::EndDisabled();
        ImGui::SameLine( 0.0f, 6.0f );
        if ( ImGui::Button( ICON_MDI_FOLDER_OPEN "  Open output", ImVec2( 150.0f, 28.0f ) ) )
            OpenOutputFolder();

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void PhotogrammetryPanel::DrawSourcePane()
    {
        auto& prefs = EditorPreferences::Get();

        ImGui::TextColored( ImVec4( 0.70f, 0.78f, 0.90f, 1.0f ), ICON_MDI_IMAGE_MULTIPLE " Source frames" );
        ImGui::Spacing();

        bool dirty = false;
        dirty |= PathPicker( "photos", prefs.PhotogrammetryPhotosDir, /*folder=*/true );
        if ( dirty )
            m_PhotosDirty = true;

        ImGui::SameLine();
        if ( ImGui::Button( ICON_MDI_REFRESH "##rescan" ) )
            m_PhotosDirty = true;
        if ( ImGui::IsItemHovered() )
            ImGui::SetTooltip( "Rescan the folder" );

        // Validation badge.
        if ( prefs.PhotogrammetryPhotosDir.empty() )
            ImGui::TextColored( kColWarn, ICON_MDI_ALERT " No photos folder set." );
        else if ( m_PhotoFiles.empty() )
            ImGui::TextColored( kColWarn,
                                ICON_MDI_ALERT " Folder has no images yet — Capture or drop photos in." );
        else
            ImGui::TextColored( kColOk, ICON_MDI_CHECK_CIRCLE " %zu image(s) ready.", m_PhotoFiles.size() );

        ImGui::Separator();

        // Thumbnail grid.
        ImGui::BeginChild( "##pgThumbs", ImVec2( 0.0f, 0.0f ), false );
        if ( m_PhotoFiles.empty() )
        {
            ImGui::TextDisabled( "Captured / dropped frames appear here." );
        }
        else if ( m_UIHelper && m_Thumbnails )
        {
            const float thumb  = 96.0f;
            const float avail  = ImGui::GetContentRegionAvail().x;
            int         perRow = static_cast<int>( avail / ( thumb + 8.0f ) );
            if ( perRow < 1 )
                perRow = 1;

            for ( int i = 0; i < static_cast<int>( m_PhotoFiles.size() ); ++i )
            {
                ImGui::PushID( i );
                auto img = m_Thumbnails->Get( m_PhotoFiles[i] );
                if ( img )
                {
                    if ( m_UIHelper->ImageButton( "##t", img, ImVec2( thumb, thumb ) ) )
                        m_SelectedPhoto = i;
                }
                else
                {
                    if ( ImGui::Button( ICON_MDI_FILE_IMAGE, ImVec2( thumb, thumb ) ) )
                        m_SelectedPhoto = i;
                }
                if ( ImGui::IsItemHovered() )
                    ImGui::SetTooltip( "%s",
                                       std::filesystem::path( m_PhotoFiles[i] ).filename().string().c_str() );
                ImGui::PopID();

                if ( ( i % perRow ) != ( perRow - 1 ) && i != static_cast<int>( m_PhotoFiles.size() ) - 1 )
                    ImGui::SameLine();
            }
        }
        ImGui::EndChild();
    }

    void PhotogrammetryPanel::DrawSettingsPane()
    {
        auto& prefs = EditorPreferences::Get();
        bool  dirty = false;

        ImGui::TextColored( ImVec4( 0.70f, 0.78f, 0.90f, 1.0f ), ICON_MDI_TUNE " Settings" );
        ImGui::Spacing();

        const bool face = ( m_Mode == Mode::Face );

        // Capture section.
        if ( ImGui::CollapsingHeader( ICON_MDI_CAMERA " Capture", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::TextDisabled( "{photos} = the photos folder; snaps frames from your camera." );
            const CmdPreset* list  = face ? kCaptureFace : kCaptureObject;
            const int        count = face ? static_cast<int>( std::size( kCaptureFace ) )
                                          : static_cast<int>( std::size( kCaptureObject ) );
            dirty |= PresetCombo( "##capPreset", list, count, prefs.PhotogrammetryCaptureCommand );
            dirty |= CommandField( "##capCmd", prefs.PhotogrammetryCaptureCommand );
        }

        // Reconstruct section.
        if ( ImGui::CollapsingHeader( ICON_MDI_CUBE_SCAN " Reconstruct", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::TextDisabled( "{input}=photos  {output}=mesh file  {outdir}=its folder" );
            const CmdPreset* list  = face ? kReconstructFace : kReconstructObject;
            const int        count = face ? static_cast<int>( std::size( kReconstructFace ) )
                                          : static_cast<int>( std::size( kReconstructObject ) );
            dirty |= PresetCombo( "##recPreset", list, count, prefs.PhotogrammetryCommand );
            dirty |= CommandField( "##recCmd", prefs.PhotogrammetryCommand );
        }

        // Import section.
        if ( ImGui::CollapsingHeader( ICON_MDI_IMPORT " Import", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::TextDisabled( "Where the tool writes the mesh (what the import reads)." );
            dirty |= PathPicker( "outmesh", prefs.PhotogrammetryOutputMesh, /*folder=*/false );
        }

        if ( dirty )
            EditorPreferences::Save();
    }

    void PhotogrammetryPanel::DrawLogPane( bool running )
    {
        ImGui::TextColored( ImVec4( 0.70f, 0.78f, 0.90f, 1.0f ), ICON_MDI_CONSOLE " Output log" );
        ImGui::SameLine();
        if ( running )
            ImGui::TextColored( kColWarn, ICON_MDI_PROGRESS_CLOCK " %s",
                                m_Job == Job::Capture ? "capturing…" : "running…" );
        ImGui::SameLine();
        if ( ImGui::SmallButton( ICON_MDI_DELETE " Clear" ) )
        {
            std::lock_guard<std::mutex> lk( m_LogMutex );
            m_Log.clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox( "Auto-scroll", &m_LogAutoScroll );

        ImGui::PushStyleColor( ImGuiCol_ChildBg, kColLogBg );
        ImGui::BeginChild( "##pgLog", ImVec2( 0.0f, 130.0f ), true, ImGuiWindowFlags_HorizontalScrollbar );
        {
            std::lock_guard<std::mutex> lk( m_LogMutex );
            for ( const auto& line : m_Log )
            {
                const bool isCmd = ( !line.empty() && line[0] == '$' );
                const bool isErr =
                     ( line.find( "error" ) != std::string::npos || line.find( "Error" ) != std::string::npos ||
                       line.find( "ERROR" ) != std::string::npos );
                if ( isCmd )
                    ImGui::TextColored( ImVec4( 0.55f, 0.80f, 1.0f, 1.0f ), "%s", line.c_str() );
                else if ( isErr )
                    ImGui::TextColored( kColErr, "%s", line.c_str() );
                else
                    ImGui::TextUnformatted( line.c_str() );
            }
        }
        if ( m_LogAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f )
            ImGui::SetScrollHereY( 1.0f );
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if ( !m_Status.empty() )
        {
            const ImVec4 col = m_StatusError ? kColErr : kColOk;
            ImGui::PushTextWrapPos( 0.0f );
            ImGui::TextColored( col, "%s", m_Status.c_str() );
            ImGui::PopTextWrapPos();
        }
    }

    // ---------------------------------------------------------------------------------------------------

    void PhotogrammetryPanel::OnUIRender()
    {
        auto& prefs = EditorPreferences::Get();

        // Header: title + mode segmented control + help.
        ImGui::TextColored( ImVec4( 0.55f, 0.80f, 1.0f, 1.0f ), ICON_MDI_CAMERA_OUTLINE );
        ImGui::SameLine( 0.0f, 6.0f );
        ImGui::TextUnformatted( "Model from Photos" );
        ImGui::SameLine( 0.0f, 16.0f );

        const auto modeButton = [&]( const char* label, Mode m )
        {
            const bool active = ( m_Mode == m );
            ImGui::PushStyleColor( ImGuiCol_Button, active ? kColAccent : ImVec4( 0.2f, 0.2f, 0.22f, 1.0f ) );
            if ( ImGui::Button( label ) )
            {
                m_Mode                   = m;
                prefs.PhotogrammetryMode = static_cast<int>( m );
                EditorPreferences::Save();
            }
            ImGui::PopStyleColor();
        };
        modeButton( ICON_MDI_CUBE_OUTLINE " Object", Mode::Object );
        ImGui::SameLine( 0.0f, 2.0f );
        modeButton( ICON_MDI_FACE_MAN " Face", Mode::Face );

        ImGui::SameLine();
        ImGui::TextDisabled( ICON_MDI_HELP_CIRCLE_OUTLINE );
        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( 420.0f );
            ImGui::TextUnformatted(
                 "The engine doesn't reconstruct 3D itself — it drives an EXTERNAL tool you have installed "
                 "(Meshroom / COLMAP / RealityCapture, or a face solver), streams its output, then imports the "
                 "produced mesh.\n\n"
                 "Object mode: photogrammetry of a physical object — shoot it from all sides.\n"
                 "Face mode: MetaHuman-style head capture — a slow sweep of the face. The solver is still an "
                 "external tool (facial landmark tracking / solve is not built into the engine).\n\n"
                 "Placeholders — {input}: photos folder, {output}: mesh file, {outdir}: its folder, "
                 "{photos}: capture target folder." );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        // Poll the worker: when it finishes, dispatch by what it was doing.
        if ( m_Done.exchange( false ) )
        {
            if ( m_Worker.joinable() )
                m_Worker.join();
            if ( m_Job == Job::Reconstruct )
            {
                ImportResult();
            }
            else if ( m_Job == Job::Capture )
            {
                m_PhotosDirty = true; // fresh frames → rescan the grid
                m_StatusError = ( m_ExitCode != 0 );

                std::string head;
                if ( m_ExitCode == 130 )
                    head = "Capture cancelled. ";
                else if ( m_ExitCode != 0 )
                    head = "Capture command exited with code " + std::to_string( m_ExitCode.load() ) + ". ";
                else
                    head = "Captured. ";
                m_Status = head + "See the frames on the left.";
            }
            m_Job = Job::None;
        }

        if ( m_PhotosDirty || m_ScannedDir != prefs.PhotogrammetryPhotosDir )
            RescanPhotos();

        const bool running = m_Running.load();

        DrawToolbar( running );
        ImGui::Spacing();

        // Main split: source frames (left) | settings (right).
        const float totalW = ImGui::GetContentRegionAvail().x;
        const float bottom = 130.0f + ImGui::GetTextLineHeightWithSpacing() * 3.5f;
        const float paneH  = ImGui::GetContentRegionAvail().y - bottom;
        const float leftW  = totalW * m_SplitRatio;
        const float rightW = totalW - leftW - 8.0f;

        ImGui::PushStyleColor( ImGuiCol_ChildBg, kColSection );
        ImGui::BeginChild( "##pgSource", ImVec2( leftW, paneH ), true );
        ImGui::PopStyleColor();
        DrawSourcePane();
        ImGui::EndChild();

        ImGui::SameLine( 0.0f, 8.0f );

        ImGui::PushStyleColor( ImGuiCol_ChildBg, kColSection );
        ImGui::BeginChild( "##pgSettings", ImVec2( rightW, paneH ), true );
        ImGui::PopStyleColor();
        DrawSettingsPane();
        ImGui::EndChild();

        ImGui::Spacing();
        DrawLogPane( running );
    }
} // namespace Desert::Editor
