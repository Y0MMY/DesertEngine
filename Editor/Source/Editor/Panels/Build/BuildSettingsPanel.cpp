#include "BuildSettingsPanel.hpp"

#include <Editor/Packaging/GamePackager.hpp>
#include <Editor/Packaging/PackageTarget.hpp>
#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/IconsMaterialDesignIcons.hpp>
#include <Editor/Core/ImGuiUtilities.hpp>

#include <Engine/Project/ProjectContext.hpp>

#include <Common/Core/JobSystem.hpp>
#include <Common/Core/Constants.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>

namespace Desert::Editor
{
    namespace ImGui = ::ImGui;

    void BuildSettingsPanel::RescanScenes()
    {
        m_Scenes.clear();
        m_ScenesScanned = true;

        if ( !::Desert::Project::ProjectContext::HasProject() )
            return;

        namespace fs               = std::filesystem;
        const fs::path  projectDir = ::Desert::Project::ProjectContext::Directory();
        const fs::path  assetsRoot = Common::Constants::Path::ASSETS_PATH;
        std::error_code ec;
        for ( const auto& entry : fs::recursive_directory_iterator( assetsRoot, ec ) )
        {
            if ( entry.is_regular_file() && entry.path().extension() == ".desce" )
                m_Scenes.push_back( fs::relative( entry.path(), projectDir, ec ).generic_string() );
        }
        std::sort( m_Scenes.begin(), m_Scenes.end() );
    }

    void BuildSettingsPanel::OnUIRender()
    {
        // The live preferences, not a copy: EditorPreferences.hpp is explicit that everything consuming a
        // preference reads it from there every time, and the one place this engine kept a second copy
        // (the gizmo snap) had to be undone by К6 when the two stopped agreeing.
        EditorPreferences& prefs = EditorPreferences::Get();

        ImGui::TextUnformatted( ( "Project: " + ::Desert::Project::ProjectContext::Current().Name ).c_str() );
        ImGui::Separator();

        // TARGET PLATFORM IS SHOWN, NOT CHOSEN. Every row is disabled and the host one is marked, because
        // this editor can only package the Runtime that was built beside it — see PackageTarget.hpp. It
        // used to be a live radio group writing an `m_Platform` that nothing anywhere read, so picking
        // "Windows x64" on macOS produced a macOS package and said nothing about it (П6).
        //
        // The rows are kept rather than replaced by a single line of text for the same reason the Linux
        // row was already disabled instead of absent: "we cannot do this here" is information, and it is
        // the answer to the question the person came to this panel with.
        ImGui::Spacing();
        ImGui::TextUnformatted( "Target platform" );
        const char* icons[] = { ICON_MDI_APPLE "  ", ICON_MDI_MICROSOFT_WINDOWS "  ", ICON_MDI_LINUX "  " };
        for ( std::size_t i = 0; i < std::size( kTargetPlatforms ); ++i )
        {
            const TargetPlatformInfo& target = kTargetPlatforms[i];
            const char*               why    = WhyNotPackageableHere( target.Platform );

            ImGui::BeginDisabled( true );
            ImGui::RadioButton( ( std::string( icons[i] ) + target.DisplayName ).c_str(), why == nullptr );
            ImGui::EndDisabled();
            if ( why != nullptr )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", why );
            }
        }
        // Wrapped, and that is not cosmetic: the first version of this text was one long unwrapped line
        // and the rendered frame cut it off inside a word, so the panel refused without saying why.
        ImGui::PushTextWrapPos( 0.0f );
        ImGui::TextDisabled( "%s", kWhyOnlyTheHostIsOffered );
        ImGui::PopTextWrapPos();

        ImGui::Spacing();
        ImGui::TextUnformatted( "Configuration" );
        for ( const char* config : { "Debug", "Release" } )
        {
            if ( ImGui::RadioButton( config, prefs.PackageConfig == config ) )
            {
                prefs.PackageConfig = config;
                EditorPreferences::Save();
            }
            if ( config[0] == 'D' )
                ImGui::SameLine();
        }

        ImGui::Spacing();
        if ( HostPlatformInfo().SupportsAppBundle )
        {
            if ( ImGui::Checkbox( ".app bundle (MoltenVK inside — no Homebrew on the player's machine)",
                                  &prefs.PackageAppBundle ) )
                EditorPreferences::Save();
        }

        ImGui::Spacing();
        ImGui::TextUnformatted( "Output folder" );
        ImGui::SetNextItemWidth( 320.0f );
        if ( Utils::ImGuiUtilities::InputText( prefs.PackageOutputDir, "##BuildOutputDir" ) )
            EditorPreferences::Save();

        ImGui::Spacing();
        ImGui::TextUnformatted( "Startup scene" );
        if ( !m_ScenesScanned )
            RescanScenes();

        const std::string current = ::Desert::Project::ProjectContext::Current().DefaultScene;
        ImGui::SetNextItemWidth( 320.0f );
        if ( ImGui::BeginCombo( "##startupScene",
                                current.empty() ? ICON_MDI_MOVIE_OPEN "  <none>" : current.c_str() ) )
        {
            // Only on an actual CHANGE. SetDefaultScene rewrites the whole .deproj — a file git tracks —
            // and ProjectContext::Save() stamps EngineVersion with this machine's commit hash and a
            // `.dirty` suffix while it is there. Re-picking the entry that is already selected used to do
            // all of that for no change at all, which is the trigger ConfigOwnership's К4 note names and
            // ConfigOwnershipCorpus's tripwire waits for. The guard is not the fix for EngineVersion —
            // К4 owns that field — it is this panel refusing to be the thing that fires it.
            if ( ImGui::Selectable( "<none>", current.empty() ) && !current.empty() )
                ::Desert::Project::ProjectContext::SetDefaultScene( "" );
            for ( const auto& scene : m_Scenes )
                if ( ImGui::Selectable( scene.c_str(), scene == current ) && scene != current )
                    ::Desert::Project::ProjectContext::SetDefaultScene( scene );
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if ( ImGui::SmallButton( ICON_MDI_REFRESH "  Rescan" ) )
            RescanScenes();

        if ( m_Scenes.empty() )
            ImGui::TextDisabled( ICON_MDI_MOVIE_OPEN "  No .desce scenes under Assets — the packaged "
                                                     "game starts empty (or pass --scene)." );
        else if ( current.empty() )
            ImGui::TextDisabled( "The packaged game boots to the chosen startup scene; saved to the .deproj." );

        ImGui::Spacing();
        const bool building = m_Building.load();
        ImGui::BeginDisabled( building );
        if ( ImGui::Button( building ? ICON_MDI_PACKAGE_VARIANT_CLOSED "  Building..."
                                     : ICON_MDI_PACKAGE_VARIANT_CLOSED "  Build",
                            ImVec2( 160.0f, 0.0f ) ) )
        {
            // Snapshot the options on the UI thread; the copy work runs on a pool worker.
            PackageOptions options;
            options.OutputDir    = prefs.PackageOutputDir;
            options.Config       = prefs.PackageConfig;
            options.MacAppBundle = prefs.PackageAppBundle;

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
            ImGui::SetTooltip( "Bakes the project into a self-contained game:\n"
                               "Runtime + Content.dpak (+ .app bundle with MoltenVK on macOS)" );

        ImGui::SameLine();
        ImGui::BeginDisabled( building );
        if ( ImGui::Button( ICON_MDI_ARCHIVE "  Build pak only", ImVec2( 160.0f, 0.0f ) ) )
        {
            m_Building.store( true );
            m_HasResult.store( false );
            Common::JobSystem::Get().Submit(
                 [this]
                 {
                     const auto result = BuildContentPak();
                     m_LastSuccess     = result.Success;
                     m_LastMessage     = result.Message;
                     m_LastPackageDir  = result.PackageDir;
                     m_HasResult.store( true );
                     m_Building.store( false );
                 } );
        }
        ImGui::EndDisabled();
        if ( ImGui::IsItemHovered() && !building )
            ImGui::SetTooltip( "Rebuilds ONLY Content.dpak next to the .deproj (no Runtime copy).\n"
                               "Also doable from scripts/CI via Tools/PakTool." );

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
