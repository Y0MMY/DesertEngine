#include "PhotogrammetryPanel.hpp"

#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Import/MeshDnD.hpp>

#include <Engine/Core/Scene.hpp>
#include <Engine/ECS/Entity.hpp>
#include <Engine/ECS/Components.hpp>

#include <Common/Core/Logger.hpp>

#include <ImGui/imgui.h>

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

        LOG_INFO( "[Photogrammetry] running: {}", cmd );
        m_Status      = "Running the external tool… (this can take minutes)";
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
                 m_Done       = true;
                 m_Running    = false;
             } );
    }

    void PhotogrammetryPanel::ImportResult()
    {
        if ( m_Worker.joinable() )
            m_Worker.join();

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
        dirty |= StringField( "Photos folder", prefs.PhotogrammetryPhotosDir );
        dirty |= StringField( "Output mesh", prefs.PhotogrammetryOutputMesh );
        dirty |= StringField( "Command", prefs.PhotogrammetryCommand );
        if ( dirty )
            EditorPreferences::Save();

        ImGui::Dummy( ImVec2( 0.0f, 6.0f ) );

        // Poll the worker: when it finishes, import + spawn on this (main) thread.
        if ( m_Done.exchange( false ) )
            ImportResult();

        const bool running = m_Running.load();
        ImGui::BeginDisabled( running );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.20f, 0.44f, 0.72f, 1.0f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.26f, 0.52f, 0.82f, 1.0f ) );
        if ( ImGui::Button( ICON_MDI_CUBE_SCAN "  Reconstruct", ImVec2( 220.0f, 30.0f ) ) )
            StartReconstruction();
        ImGui::PopStyleColor( 2 );
        ImGui::EndDisabled();

        if ( running )
        {
            ImGui::SameLine();
            ImGui::TextColored( ImVec4( 0.95f, 0.75f, 0.35f, 1.0f ), ICON_MDI_PROGRESS_CLOCK " Running…" );
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
