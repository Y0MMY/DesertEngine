#include "EditorPreferences.hpp"

#include <Editor/Core/GizmoState.hpp>

#include <Engine/Graphic/RenderConfig.hpp>
#include <Engine/Project/ProjectContext.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <rflcpp/rfl/json.hpp>

// glm::vec3 <-> JSON reflector (OutlineColor). Must be visible before the rfl::json read/write below.
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <algorithm>
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
        // MSAA is consumed by SceneRenderer::Init — Load() runs in the EditorLayer constructor,
        // before any render system initializes, so a startup-baked setting lands in time.
        Graphic::RenderConfig::MSAASamples = p.MSAASamples;

        Core::GizmoState::SetTranslateSnap( p.TranslateSnap );
        Core::GizmoState::SetRotateSnapDegrees( p.RotateSnapDeg );
        Core::GizmoState::SetScaleSnap( p.ScaleSnap );
        Core::GizmoState::SetPersistentSnap( p.PersistentSnap );
    }

    void EditorPreferences::Load()
    {
        // First run: no prefs file yet — keep defaults, and skip the read's "could not read file"
        // error line, which would be noise for a state that is expected.
        if ( !std::filesystem::exists( PrefsFile() ) )
        {
            ApplyToGizmoState( Get() );
            return;
        }

        const std::string raw = Common::Utils::FileSystem::ReadFileContent( PrefsFile() );
        if ( !raw.empty() )
        {
            // DefaultIfMissing: prefs written by older builds (fewer fields) keep loading — new
            // fields just take their in-struct defaults instead of failing the whole file.
            if ( auto parsed = rfl::json::read<EditorPreferences, rfl::DefaultIfMissing>( raw );
                 parsed.has_value() )
                Get() = parsed.value();
            else
                LOG_WARN( "[Prefs] editor.json is corrupt, using defaults: {}", parsed.error().what() );
        }
        ApplyToGizmoState( Get() );
    }

    bool EditorPreferences::IsFavouriteField( const std::string& key )
    {
        const auto& v = Get().FavouriteFields;
        return std::find( v.begin(), v.end(), key ) != v.end();
    }

    void EditorPreferences::ToggleFavouriteField( const std::string& key )
    {
        auto& v  = Get().FavouriteFields;
        auto  it = std::find( v.begin(), v.end(), key );
        if ( it != v.end() )
            v.erase( it );
        else
            v.push_back( key );
        Save();
    }

    bool EditorPreferences::IsComponentCollapsed( const std::string& name )
    {
        const auto& v = Get().CollapsedComponents;
        return std::find( v.begin(), v.end(), name ) != v.end();
    }

    void EditorPreferences::SetComponentCollapsed( const std::string& name, bool collapsed )
    {
        auto& v  = Get().CollapsedComponents;
        auto  it = std::find( v.begin(), v.end(), name );
        if ( collapsed == ( it != v.end() ) )
            return; // already in the requested state — don't rewrite the file for nothing
        if ( collapsed )
            v.push_back( name );
        else
            v.erase( it );
        Save();
    }

    void EditorPreferences::Save()
    {
        ApplyToGizmoState( Get() );
        Common::Utils::FileSystem::WriteContentToFile( std::filesystem::path( PrefsFile() ),
                                                       rfl::json::write( Get() ) );
        LOG_INFO( "[Prefs] Saved {}", PrefsFile() );
    }
} // namespace Desert::Editor
