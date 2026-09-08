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

        // The icon is chosen BY THE PLATFORM, not by position. A parallel array indexed alongside
        // kTargetPlatforms is two things that must agree, and reordering the table would silently put
        // the Apple logo on the Windows row — the exact defect shape this task is about.
        const auto iconOf = []( TargetPlatform platform ) -> const char*
        {
            switch ( platform )
            {
                case TargetPlatform::MacOS:
                    return ICON_MDI_APPLE "  ";
                case TargetPlatform::Windows:
                    return ICON_MDI_MICROSOFT_WINDOWS "  ";
                case TargetPlatform::Linux:
                    return ICON_MDI_LINUX "  ";
            }
            return "";
        };

        for ( const TargetPlatformInfo& target : kTargetPlatforms )
        {
            const char* why = WhyNotPackageableHere( target.Platform );

            ImGui::BeginDisabled( true );
            ImGui::RadioButton( ( std::string( iconOf( target.Platform ) ) + target.DisplayName ).c_str(),
                                why == nullptr );
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
        const char* configs[] = { "Debug", "Release" };
        for ( std::size_t i = 0; i < std::size( configs ); ++i )
        {
            if ( i > 0 )
                ImGui::SameLine();
            if ( ImGui::RadioButton( configs[i], prefs.PackageConfig == configs[i] ) )
            {
                prefs.PackageConfig = configs[i];
                EditorPreferences::Save();
            }
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
            // Only on an actual CHANGE. SetDefaultScene rewrites the whole .deproj, which git tracks, so
            // re-picking the entry that is already selected used to produce a diff for no change at all.
            //
            // The guard was added while ProjectContext::Save() still stamped EngineVersion with this
            // machine's commit hash, which made the pointless rewrite actively harmful; К11 removed the
            // stamp, so what is left is ordinary hygiene — a panel does not dirty a shared file because
            // somebody opened a combo box.
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
            // REVEAL IS macOS-ONLY ON PURPOSE, and this is a decision rather than an omission.
            //
            // `std::system` is `/bin/sh -c`, so the path has to survive a shell. Tools/ProjectHub/Source/
            // Launch.hpp measured what that costs with a folder name: `$HOME` expanded, a double quote
            // killed the launch outright, and a BACKTICK EXECUTED ITS CONTENTS. Its answer was to stop
            // using a shell at all — an argv array through posix_spawn / CreateProcessW — and its own
            // comment says "the same rule closes Reveal in Finder, which had the identical splice". That
            // utility lives in the launcher and is not reachable from the Editor, so this line is
            // single-quoted instead, which IS complete for `/bin/sh`: inside single quotes the shell
            // expands nothing, and the only character needing care is the quote itself.
            //
            // The Windows half is not written here for the same reason: cmd.exe has no equivalent of
            // single quoting, so adding an `explorer "..."` line would be adding the very splice the
            // paragraph above is about, on the platform where it is hardest to get right. It waits for
            // the spawn utility to be hoisted out of ProjectHub into Common — which also owns the three
            // copies of this same splice in PhotogrammetryPanel.cpp (lines 585-589).
            if ( HostPlatformInfo().Platform == TargetPlatform::MacOS && m_LastSuccess &&
                 ImGui::Button( ICON_MDI_FOLDER_OPEN "  Reveal in Finder" ) )
            {
                std::string quoted = "'";
                for ( const char c : m_LastPackageDir )
                    quoted += c == '\'' ? std::string( "'\\''" ) : std::string( 1, c );
                quoted += "'";
                std::system( ( "open " + quoted ).c_str() );
            }
        }
    }
} // namespace Desert::Editor
