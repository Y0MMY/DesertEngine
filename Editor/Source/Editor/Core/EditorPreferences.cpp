#include "EditorPreferences.hpp"

#include <Editor/Core/GizmoState.hpp>

#include <Engine/Project/ProjectContext.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <rflcpp/rfl/json.hpp>

#include <cstdlib>
#include <filesystem>

namespace Desert::Editor
{
    EditorPreferences& EditorPreferences::Get()
    {
        static EditorPreferences s_Instance;
        return s_Instance;
    }

    std::string EditorPreferences::ConfigDirectory()
    {
        // Single source of truth for the user config dir lives with the engine's project system.
        return ::Desert::Project::ProjectContext::ConfigDirectory();
    }

    static std::string PrefsFile()
    {
        return EditorPreferences::ConfigDirectory() + "/editor.json";
    }

    static void ApplyToGizmoState( const EditorPreferences& p )
    {
        Core::GizmoState::SetTranslateSnap( p.TranslateSnap );
        Core::GizmoState::SetRotateSnapDegrees( p.RotateSnapDeg );
        Core::GizmoState::SetScaleSnap( p.ScaleSnap );
        Core::GizmoState::SetPersistentSnap( p.PersistentSnap );
    }

    void EditorPreferences::Load()
    {
        // First run: no prefs file yet — keep defaults (ReadFileContent ASSERTS on missing files).
        if ( !std::filesystem::exists( PrefsFile() ) )
        {
            ApplyToGizmoState( Get() );
            return;
        }

        const std::string raw = Common::Utils::FileSystem::ReadFileContent( PrefsFile() );
        if ( !raw.empty() )
        {
            if ( auto parsed = rfl::json::read<EditorPreferences>( raw ); parsed.has_value() )
                Get() = parsed.value();
            else
                LOG_WARN( "[Prefs] editor.json is corrupt, using defaults: {}", parsed.error().what() );
        }
        ApplyToGizmoState( Get() );
    }

    void EditorPreferences::Save()
    {
        ApplyToGizmoState( Get() );
        Common::Utils::FileSystem::WriteContentToFile( std::filesystem::path( PrefsFile() ),
                                                       rfl::json::write( Get() ) );
        LOG_INFO( "[Prefs] Saved {}", PrefsFile() );
    }
} // namespace Desert::Editor
