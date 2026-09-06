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

        const auto raw = Common::Utils::FileSystem::ReadFileContent( PrefsFile() );
        if ( raw && !raw.GetValue().empty() )
        {
            // DefaultIfMissing: prefs written by older builds (fewer fields) keep loading — new
            // fields just take their in-struct defaults instead of failing the whole file.
            if ( auto parsed = rfl::json::read<EditorPreferences, rfl::DefaultIfMissing>( raw.GetValue() );
                 parsed.has_value() )
                Get() = parsed.value();
            else
                LOG_WARN( "[Prefs] editor.json is corrupt, using defaults: {}", parsed.error().what() );
        }

        // METRE-ERA TranslateSnap -> centimetres, once, at load, written back below.
        //
        // The field's default was 0.5 with the comment "world units" from when a unit was a metre. A
        // world unit is a CENTIMETRE now, so every stored value is a hundredth of what its author meant,
        // and the Preferences slider that wrote it was labelled "Move (m)" while feeding a centimetre
        // system. Sub-centimetre snapping is meaningless at this scale — the smallest step the editor
        // offers is 1 cm — so a stored value below 1 can only be a metre-era number.
        //
        // EXPIRY: this raises pre-2026-09 preference files and nothing else. Delete it once no one is
        // carrying a config written before У5; it cannot fire on a value this build can produce.
        if ( Get().TranslateSnap > 0.0f && Get().TranslateSnap < 1.0f )
        {
            const float old = Get().TranslateSnap;
            Get().TranslateSnap *= 100.0f;
            LOG_INFO( "[Prefs] Grid snap {} was a metre-era value; migrated to {} cm (1 unit = 1 cm).", old,
                      Get().TranslateSnap );
            Save(); // written back in the new form: the migration runs once, not every launch
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

    bool EditorPreferences::Save()
    {
        ApplyToGizmoState( Get() );
        // Load() checks its read and handles a corrupt file; Save() logged success unconditionally, so
        // preferences silently stopped persisting the moment the config directory became unwritable.
        if ( const auto written = Common::Utils::FileSystem::WriteContentToFileAtomic(
                  std::filesystem::path( PrefsFile() ), rfl::json::write( Get() ) );
             !written )
        {
            LOG_ERROR( "[Prefs] {} was NOT saved: {} — these settings apply to this session only.", PrefsFile(),
                       written.GetError() );
            return false;
        }
        LOG_INFO( "[Prefs] Saved {}", PrefsFile() );
        return true;
    }
} // namespace Desert::Editor
