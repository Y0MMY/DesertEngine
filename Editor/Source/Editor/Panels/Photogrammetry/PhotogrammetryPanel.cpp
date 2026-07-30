#include "PhotogrammetryPanel.hpp"

#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Import/MeshDnD.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

#include <Common/Core/Logger.hpp>

#include <ImGui/imgui.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    namespace
    {
        // Substitutes {input}/{output}/{outdir} in the command template.
        std::string Substitute( std::string cmd, const std::string& input, const std::string& output )
        {
            const std::string outdir  = std::filesystem::path( output ).parent_path().string();
            const auto        replace = [&]( const char* token, const std::string& value )
            {
                for ( size_t p = cmd.find( token ); p != std::string::npos; p = cmd.find( token, p ) )
                    cmd.replace( p, std::string( token ).size(), value );
            };
            replace( "{input}", input );
            replace( "{output}", output );
            replace( "{outdir}", outdir );
            return cmd;
        }

        // Fixed-size ImGui text field backed by a std::string (copied in/out around the widget).
        bool StringField( const char* label, std::string& value, float width = -1.0f )
        {
            char buf[1024];
            std::snprintf( buf, sizeof( buf ), "%s", value.c_str() );
            if ( width != 0.0f )
                ImGui::SetNextItemWidth( width );
            const bool changed = ImGui::InputText( label, buf, sizeof( buf ) );
            if ( changed )
                value = buf;
            return changed;
        }

        // A path field with a native "Browse…" button: [ editable field ][ Browse ][ label ]. `folder` picks a
        // directory (OpenFolderDialog); otherwise a save-file dialog (the output mesh). Edits + a picked path
        // both update `value`.
        bool PathPicker( const char* id, const char* label, std::string& value, bool folder )
        {
            constexpr float btnW = 92.0f;
            ImGui::PushID( id );

            char buf[1024];
            std::snprintf( buf, sizeof( buf ), "%s", value.c_str() );
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - btnW - 150.0f );
            bool changed = ImGui::InputText( "##field", buf, sizeof( buf ) );
            if ( changed )
                value = buf;

            ImGui::SameLine();
            if ( ImGui::Button( ICON_MDI_FOLDER_OPEN " Browse", ImVec2( btnW, 0.0f ) ) )
            {
                const auto picked =
                     folder ? Common::Utils::FileSystem::OpenFolderDialog()
                            : Common::Utils::FileSystem::SaveFileDialog( "Meshes\0*.obj;*.glb;*.gltf;*.fbx;*.ply\0"
                                                                         "All\0*.*\0" );
                if ( !picked.empty() )
                {
                    value   = picked.string();
                    changed = true;
                }
            }
            ImGui::SameLine();
            ImGui::TextUnformatted( label );

            ImGui::PopID();
            return changed;
        }
    } // namespace

    PhotogrammetryPanel::PhotogrammetryPanel( const std::shared_ptr<::Desert::Core::Scene>& scene,
                                              Assets::AssetManager*                         assets )
         : IPanel( "Model from Photos", /*showPanel=*/false ), m_Scene( scene ), m_Assets( assets )
    {
    }

    PhotogrammetryPanel::~PhotogrammetryPanel()
    {
        if ( m_Worker.joinable() )
            m_Worker.join();
    }

    void PhotogrammetryPanel::RunCommand( const std::string& cmd, Job job )
    {
        LOG_INFO( "[Photogrammetry] running: {}", cmd );
        m_Job         = job;
        m_StatusError = false;
        m_Running     = true;
        m_Done        = false;

        if ( m_Worker.joinable() )
            m_Worker.join();
        m_Worker = std::thread(
             [this, cmd]()
             {
                 const int rc = std::system( cmd.c_str() ); // blocking, on this worker thread
                 m_ExitCode   = rc;
                 m_Running    = false;
                 m_Done       = true;
             } );
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

        m_Status = "Capturing frames from the camera… move slowly around the object.";
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
        if ( !std::filesystem::exists( m_OutputCaptured, ec ) )
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

        auto& e = m_Scene->CreateNewEntity( "Photogrammetry Model" );
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

    void PhotogrammetryPanel::OnUIRender()
    {
        auto& prefs = EditorPreferences::Get();

        ImGui::TextColored( ImVec4( 0.55f, 0.80f, 1.0f, 1.0f ), ICON_MDI_CAMERA_OUTLINE );
        ImGui::SameLine( 0.0f, 6.0f );
        ImGui::TextUnformatted( "Model from Photos (photogrammetry)" );
        ImGui::SameLine();
        ImGui::TextDisabled( ICON_MDI_HELP_CIRCLE_OUTLINE );
        if ( ImGui::IsItemHovered() )
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( 380.0f );
            ImGui::TextUnformatted(
                 "The engine doesn't reconstruct 3D itself — it runs an EXTERNAL photogrammetry tool "
                 "(Meshroom / AliceVision, COLMAP, ...) you have installed, then imports its output mesh.\n\n"
                 "Command placeholders:\n"
                 "  {input}  - the photos folder\n"
                 "  {output} - the produced mesh file (what the import reads)\n"
                 "  {outdir} - the folder of {output}\n\n"
                 "Point {output} at whatever file your tool actually writes (OBJ/GLB/FBX)." );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0.0f, 4.0f ) );

        bool dirty = false;
        dirty |= PathPicker( "photos", "Photos folder", prefs.PhotogrammetryPhotosDir, /*folder=*/true );
        dirty |= PathPicker( "outmesh", "Output mesh", prefs.PhotogrammetryOutputMesh, /*folder=*/false );

        // Poll the worker: when it finishes, dispatch by what it was doing (import a mesh, or report a capture).
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
                std::error_code ec;
                int             images = 0;
                for ( const auto& f : std::filesystem::directory_iterator( prefs.PhotogrammetryPhotosDir, ec ) )
                {
                    std::string ext = f.path().extension().string();
                    for ( auto& c : ext )
                        c = static_cast<char>( std::tolower( c ) );
                    if ( ext == ".jpg" || ext == ".jpeg" || ext == ".png" )
                        ++images;
                }
                m_StatusError = ( m_ExitCode != 0 );

                std::string head;
                if ( m_ExitCode != 0 )
                    head = "Capture command exited with code " + std::to_string( m_ExitCode.load() ) + ". ";
                else
                    head = "Captured — ";

                m_Status = head + std::to_string( images ) + " image(s) in the photos folder.";
            }
            m_Job = Job::None;
        }

        const bool running = m_Running.load();

        // Step 1: capture frames from the camera into the photos folder (tool-agnostic).
        ImGui::Dummy( ImVec2( 0.0f, 6.0f ) );
        ImGui::TextColored( ImVec4( 0.70f, 0.78f, 0.90f, 1.0f ), ICON_MDI_CAMERA " 1. Capture from camera" );
        dirty |= StringField( "Capture cmd", prefs.PhotogrammetryCaptureCommand );
        ImGui::BeginDisabled( running );
        if ( ImGui::Button( ICON_MDI_CAMERA "  Capture", ImVec2( 160.0f, 26.0f ) ) )
            StartCapture();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled( "{photos} = the photos folder; snaps a burst from your webcam." );

        // Step 2: reconstruct a mesh from the captured/collected photos.
        ImGui::Dummy( ImVec2( 0.0f, 8.0f ) );
        ImGui::TextColored( ImVec4( 0.70f, 0.78f, 0.90f, 1.0f ), ICON_MDI_CUBE_SCAN " 2. Reconstruct" );
        dirty |= StringField( "Reconstruct cmd", prefs.PhotogrammetryCommand );
        ImGui::BeginDisabled( running );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.44f, 0.72f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.52f, 0.82f, 1.0f ) );
        if ( ImGui::Button( ICON_MDI_CUBE_SCAN "  Reconstruct", ImVec2( 220.0f, 30.0f ) ) )
            StartReconstruction();
        ImGui::PopStyleColor( 2 );
        ImGui::EndDisabled();

        if ( dirty )
            EditorPreferences::Save();

        if ( running )
        {
            ImGui::SameLine();
            ImGui::TextColored( ImVec4( 0.95f, 0.75f, 0.35f, 1.0f ), m_Job == Job::Capture
                                                                          ? ICON_MDI_PROGRESS_CLOCK " Capturing…"
                                                                          : ICON_MDI_PROGRESS_CLOCK " Running…" );
        }

        if ( !m_Status.empty() )
        {
            ImGui::Dummy( ImVec2( 0.0f, 6.0f ) );
            const ImVec4 col =
                 m_StatusError ? ImVec4( 0.95f, 0.45f, 0.45f, 1.0f ) : ImVec4( 0.55f, 0.85f, 0.55f, 1.0f );
            ImGui::PushTextWrapPos( 0.0f );
            ImGui::TextColored( col, "%s", m_Status.c_str() );
            ImGui::PopTextWrapPos();
        }
    }
} // namespace Desert::Editor
