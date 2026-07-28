#include "BuildSettingsPanel.hpp"

#include <Editor/Packaging/GamePackager.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Project/ProjectContext.hpp>

#include <Common/Core/JobSystem.hpp>

#include <cstdlib>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void BuildSettingsPanel::OnUIRender()
    {
        ImGui::TextUnformatted(
             ( "Project: " + ::Desert::Project::ProjectContext::Current().Name ).c_str() );
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::TextUnformatted( "Target platform" );
        const char* platforms[] = { ICON_MDI_APPLE "  macOS (Apple Silicon)", ICON_MDI_MICROSOFT_WINDOWS "  Windows x64",
                                    ICON_MDI_LINUX "  Linux x64 (planned)" };
        for ( int i = 0; i < 3; ++i )
        {
            const bool selectable = i < 2; // Linux is a placeholder row
            ImGui::BeginDisabled( !selectable );
            if ( ImGui::RadioButton( platforms[i], m_Platform == i ) && selectable )
                m_Platform = i;
            ImGui::EndDisabled();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted( "Configuration" );
        ImGui::RadioButton( "Debug", &m_Config, 0 );
        ImGui::SameLine();
        ImGui::RadioButton( "Release", &m_Config, 1 );

        ImGui::Spacing();
        ImGui::TextUnformatted( "Output folder" );
        ImGui::SetNextItemWidth( 320.0f );
        Utils::ImGuiUtilities::InputText( m_OutputDir, "##BuildOutputDir" );

        ImGui::Spacing();
        ImGui::TextUnformatted( "Scenes in build" );
        ImGui::BeginChild( "##buildScenes", ImVec2( 0.0f, 60.0f ), true );
        {
            const auto& scene = ::Desert::Project::ProjectContext::Current().DefaultScene;
            if ( scene.empty() )
                ImGui::TextDisabled( ICON_MDI_MOVIE_OPEN "  No DefaultScene in the .deproj — the packaged "
                                                         "game starts empty (or pass --scene)." );
            else
                ImGui::Text( ICON_MDI_MOVIE_OPEN "  %s (startup scene)", scene.c_str() );
        }
        ImGui::EndChild();

        ImGui::Spacing();
        const bool building = m_Building.load();
        ImGui::BeginDisabled( building );
        if ( ImGui::Button( building ? ICON_MDI_PACKAGE_VARIANT_CLOSED "  Building..."
                                     : ICON_MDI_PACKAGE_VARIANT_CLOSED "  Build",
                            ImVec2( 160.0f, 0.0f ) ) )
        {
            // Snapshot the options on the UI thread; the copy work runs on a pool worker.
            PackageOptions options;
            options.OutputDir = m_OutputDir;
            options.Config    = m_Config == 0 ? "Debug" : "Release";

            m_Building.store( true );
            m_HasResult.store( false );
            Common::JobSystem::Get().Submit(
                 [this, options]
                 {
                     const auto result = PackageGame( options );
                     m_LastSuccess     = result.Success;
                     m_LastMessage     = result.Message;
                     m_LastPackageDir  = result.PackageDir;
                     m_HasResult.store( true );
                     m_Building.store( false );
                 } );
        }
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered() && !building )
            ImGui::SetTooltip( "Bakes the project into a self-contained game folder:\n"
                               "Runtime + Assets (raw mesh sources stripped) + Cooked + shaders + run.sh" );

        if ( m_HasResult.load() )
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos( 0.0f );
            ImGui::TextColored( m_LastSuccess ? ImVec4( 0.5f, 0.9f, 0.5f, 1.0f )
                                              : ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ),
                                "%s", m_LastMessage.c_str() );
            ImGui::PopTextWrapPos();
#ifdef DESERT_PLATFORM_MACOS
            if ( m_LastSuccess && ImGui::Button( ICON_MDI_FOLDER_OPEN "  Reveal in Finder" ) )
                std::system( ( "open \"" + m_LastPackageDir + "\"" ).c_str() );
#endif
        }
    }
} // namespace Desert::Editor
