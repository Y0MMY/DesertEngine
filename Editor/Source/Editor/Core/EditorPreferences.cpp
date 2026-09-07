#include "EditorPreferences.hpp"

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

    // The renderer's copy of the one preference a Vulkan-side system has to see for itself.
    // Engine/Graphic must not know the editor exists, so the value is PUSHED down a layer rather than
    // pulled up one. MSAA is consumed by SceneRenderer::Init — Load() runs in the EditorLayer
    // constructor, before any render system initializes, so a startup-baked setting lands in time, and
    // Save() repeats it because a second viewport creates a SceneRenderer mid-session and that new
    // renderer must bake the CURRENT selection rather than the one this process started with.
    //
    // THIS IS NOT THE SIDE EFFECT THAT USED TO SHARE THIS FUNCTION. Its predecessor was called
    // ApplyToGizmoState and pushed the four gizmo snap values into Core::GizmoState, which was a second
    // STORE for them that four other places also wrote — so running it from Save() overwrote whatever
    // the user had just chosen with whatever was last loaded, on every unrelated save. There is no such
    // hazard here and the difference is structural, not a matter of degree: RenderConfig::MSAASamples
    // has exactly ONE writer, this line, and is derived from exactly one source, the field beside it.
    // Re-running it can only restate what the owner already says. A push is safe precisely when the
    // side being pushed to is not also an authority on the value.
    static void PushToRenderConfig( const EditorPreferences& p )
    {
        Graphic::RenderConfig::MSAASamples = p.MSAASamples;
    }

    void EditorPreferences::Load()
    {
        // First run: no prefs file yet — keep defaults, and skip the read's "could not read file"
        // error line, which would be noise for a state that is expected.
        if ( !std::filesystem::exists( PrefsFile() ) )
        {
            PushToRenderConfig( Get() );
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

        PushToRenderConfig( Get() );
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
        // A SAVE MUST NOT CHANGE A SINGLE FIELD THE USER DID NOT TOUCH. This function's first statement
        // used to be ApplyToGizmoState(), which reverted the four gizmo snap values to whatever this
        // struct last held — so toggling the Perf HUD, picking an MSAA level or starring a field in
        // Details silently undid a snap step chosen from the toolbar. The line below is the whole of
        // what a save is now allowed to do besides writing the file, and its header says why it cannot
        // have the same effect. Desert/Tests/Editor/PreferenceOwnership asserts the relation.
        PushToRenderConfig( Get() );
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
