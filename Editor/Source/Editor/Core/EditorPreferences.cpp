#include "EditorPreferences.hpp"

#include <Engine/Graphic/RenderConfig.hpp>
#include <Engine/Project/ProjectContext.hpp>

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Core/Logger.hpp>

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/json.hpp>

// glm::vec3 <-> JSON reflector (OutlineColor). Must be visible before the rfl::json read/write below.
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iterator>

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

    // WHAT THIS PROCESS BELIEVES editor.json HOLDS, in the canonical text Save() writes. Empty means it
    // believes there is no file (first run), or that it cannot claim to know — a failed read, a corrupt
    // parse — in which case the next write must happen unconditionally.
    //
    // IT IS A MEMO, NOT A SECOND STORE, and that distinction is the whole of why it is safe. Nothing ever
    // reads a preference out of it; it is only compared, as text, against what is about to be written. The
    // struct above stays the sole authority on every value, so this cannot become the К6 shape (one value,
    // two stores, writers disagreeing about the authority) however it drifts — the worst a stale memo can
    // do is skip a write, and the file's own contents are what it is stale WITH RESPECT TO.
    //
    // It exists because К8 made "write the same bytes again" the common case rather than a rarity: every
    // control in the Preferences window now commits when the user lets go of it, and three other panels
    // commit on a click. Letting go of a control you only hovered, re-picking the MSAA level you are
    // already on, or dragging a slider back where it started were each a file write and a log line.
    static std::string s_OnDisk;

    // Which top-level keys of editor.json differ between two of its serializations, by name.
    //
    // THE KEY LIST IS THE UNION OF WHAT THE TWO TEXTS ACTUALLY CONTAIN, and it used to be
    // rfl::fields<EditorPreferences>(). Both derive the list rather than typing it, so both were
    // field-count-proof; the difference is that the struct's field list stopped being the whole of the
    // file's key list when UnknownKeys arrived. A key held by another build is a key of this file, and
    // this is the function that has to be able to say a migration dropped one — asking rfl::fields<>
    // about it would name nothing, which is the "container derived from the same source as the question"
    // shape the contract's §1.4 warns about, in miniature.
    static std::vector<std::string> ChangedFields( const std::string& before, const std::string& after )
    {
        const auto lhs = rfl::json::read<rfl::Generic>( before );
        const auto rhs = rfl::json::read<rfl::Generic>( after );
        if ( !lhs.has_value() || !rhs.has_value() )
            return {};

        const auto lhsObject = lhs.value().to_object();
        const auto rhsObject = rhs.value().to_object();
        if ( !lhsObject.has_value() || !rhsObject.has_value() )
            return {};

        std::vector<std::string> keys;
        for ( const auto& [name, value] : lhsObject.value() )
            keys.push_back( name );
        for ( const auto& [name, value] : rhsObject.value() )
            if ( std::find( keys.begin(), keys.end(), name ) == keys.end() )
                keys.push_back( name );

        std::vector<std::string> differing;
        for ( const std::string& key : keys )
        {
            const auto a = lhsObject.value().get( key );
            const auto b = rhsObject.value().get( key );
            if ( !a.has_value() || !b.has_value() )
            {
                differing.push_back( key );
                continue;
            }
            if ( rfl::json::write( a.value() ) != rfl::json::write( b.value() ) )
                differing.push_back( key );
        }
        return differing;
    }

    // THE EVENT, not the call. `LOG_INFO( "[Prefs] Saved {}", path )` fired on every save and said only
    // that this function had run — which is the least informative thing it knows, and with three panels
    // saving on a click the Logs panel filled with identical lines that named neither the setting nor the
    // panel. Commit-on-edit would have made that strictly worse. What a reader needs is which settings
    // moved, and the diff against what is already on disk has that for free.
    static std::string DescribeChange( const std::string& before, const std::string& after )
    {
        if ( before.empty() )
            return "created";

        const auto changed = ChangedFields( before, after );
        if ( changed.empty() )
        {
            // The texts differ but no field does: only reachable if the JSON writer's own output changes
            // between builds. Said plainly rather than reported as a settings change that did not happen.
            return "reserialized";
        }

        std::string list = changed.front();
        for ( std::size_t i = 1; i < changed.size(); ++i )
            list += ", " + changed[i];
        return list;
    }

    // THE ONLY WRITER OF editor.json. Both public entry points funnel here; they differ in one thing, the
    // sentence that reaches the log, which is exactly the difference between "the user changed a setting"
    // and "this build raised a stored value".
    //
    // `event` empty means "derive it from what actually differs" — Save() has no sentence of its own to
    // offer, and the diff is a better one than any fixed string.
    static bool PersistCurrent( std::string event )
    {
        // A SAVE MUST NOT CHANGE A SINGLE FIELD THE USER DID NOT TOUCH. This was once the first statement
        // of Save() as ApplyToGizmoState(), which reverted the four gizmo snap values to whatever this
        // struct last held — so toggling the Perf HUD, picking an MSAA level or starring a field in Details
        // silently undid a snap step chosen from the toolbar. The line below is the whole of what a save is
        // allowed to do besides writing the file, and PushToRenderConfig's header says why it cannot have
        // the same effect. Desert/Tests/Editor/PreferenceOwnership asserts the relation.
        PushToRenderConfig( EditorPreferences::Get() );

        const std::string json = rfl::json::write( EditorPreferences::Get() );
        if ( json == s_OnDisk && std::filesystem::exists( PrefsFile() ) )
        {
            // The file already says exactly this. No write, and no log line about one — a line saying
            // nothing changed is the same noise as a line saying something did, only wronger.
            //
            // THE exists() IS NOT BELT AND BRACES. Without it, `true` would mean "the memo says the disk
            // agrees" rather than "the disk agrees", and a file removed behind a running editor would
            // never be rebuilt — a successful answer that is silently wrong, which is exactly what §1.4
            // forbids. It only runs when the memo already matched, so an ordinary save pays nothing.
            return true;
        }

        if ( event.empty() )
            event = DescribeChange( s_OnDisk, json );

        // Load() checks its read and handles a corrupt file; Save() logged success unconditionally, so
        // preferences silently stopped persisting the moment the config directory became unwritable.
        const auto written =
             Common::Utils::FileSystem::WriteContentToFileAtomic( std::filesystem::path( PrefsFile() ), json );
        if ( !written )
        {
            // The memo is deliberately NOT updated here: the next save must try again rather than assume
            // the disk agrees with us.
            LOG_ERROR( "[Prefs] {} was NOT saved: {} — these settings apply to this session only.", PrefsFile(),
                       written.GetError() );
            return false;
        }

        s_OnDisk = json;
        LOG_INFO( "[Prefs] {} -> {}", event, PrefsFile() );
        return true;
    }

    // KEYS THIS PROJECT DELETED ON PURPOSE, WHICH IS A DIFFERENT THING FROM A KEY IT DOES NOT KNOW.
    //
    // Before К9 a removed field needed no entry here: the struct stopped naming it, so the next save
    // rewrote the file without it and the key was gone. UnknownKeys ended that — every key survives now,
    // including the ones somebody meant to destroy — so the deletion has to be stated somewhere, and this
    // is that somewhere. The invariant it protects is contract §4's: a retirement finishes.
    //
    // Both entries are К1's. `PhotogrammetryCaptureCommand` documented a `{photos}` substitution that was
    // never implemented and `PhotogrammetryMode` an Object/Face preset switch that does not exist; they
    // were serialized into every editor.json and read by nothing.
    //
    // EXPIRY: a row leaves this list when no config in circulation can still carry the key. It costs one
    // string compare per unknown key per launch, and a launch has neither in the ordinary case.
    static bool IsRetiredKey( const std::string& key )
    {
        static const std::vector<std::string> retired = { "PhotogrammetryCaptureCommand", "PhotogrammetryMode" };
        return std::find( retired.begin(), retired.end(), key ) != retired.end();
    }

    std::vector<std::string> EditorPreferences::MigrateLoaded( EditorPreferences& p )
    {
        std::vector<std::string> raised;

        // The retired keys, dropped by name. Rebuilt rather than erased in place because rfl::Object is
        // an ordered vector of pairs with no erase() — and rebuilding preserves the order of what is
        // kept, which matters because the memo in PersistCurrent compares the serialized TEXT.
        if ( !p.UnknownKeys.empty() )
        {
            rfl::ExtraFields<rfl::Generic> kept;
            for ( const auto& [key, value] : p.UnknownKeys )
            {
                if ( !IsRetiredKey( key ) )
                {
                    kept.insert( key, value );
                    continue;
                }
                raised.push_back( "retired key '" + key + "' dropped (deleted by К1; it was read by nothing)" );
            }
            p.UnknownKeys = std::move( kept );
        }

        // METRE-ERA TranslateSnap -> centimetres.
        //
        // The field's default was 0.5 with the comment "world units" from when a unit was a metre. A
        // world unit is a CENTIMETRE now, so every stored value is a hundredth of what its author meant,
        // and the Preferences slider that wrote it was labelled "Move (m)" while feeding a centimetre
        // system. Sub-centimetre snapping is meaningless at this scale — the smallest step the editor
        // offers is 1 cm — so a stored value below 1 can only be a metre-era number.
        //
        // EXPIRY: this raises pre-2026-09 preference files and nothing else. Delete it once no one is
        // carrying a config written before У5; it cannot fire on a value this build can produce.
        if ( p.TranslateSnap > 0.0f && p.TranslateSnap < 1.0f )
        {
            const float old = p.TranslateSnap;
            p.TranslateSnap *= 100.0f;
            raised.push_back( "TranslateSnap " + std::to_string( old ) + " -> " +
                              std::to_string( p.TranslateSnap ) + " cm (1 world unit = 1 cm)" );
        }

        return raised;
    }

    void EditorPreferences::Load()
    {
        // Anything short of a file we read AND understood leaves the memo empty, which means "we cannot
        // claim the disk agrees with us" and forces the next save to write. That is the safe direction:
        // the alternative is a corrupt or unreadable editor.json that a later identical-looking save
        // declines to repair.
        s_OnDisk.clear();

        // First run: no prefs file yet — keep defaults, and skip the read's "could not read file"
        // error line, which would be noise for a state that is expected.
        if ( !std::filesystem::exists( PrefsFile() ) )
        {
            PushToRenderConfig( Get() );
            return;
        }

        if ( const auto raw = Common::Utils::FileSystem::ReadFileContent( PrefsFile() ); !raw )
        {
            // The file EXISTS — that is tested above — so a failed read is a permission or I/O problem
            // and not the expected first-run case. It used to fall through a bare `if ( raw && ... )`
            // with nothing logged, which is §1.4's silent fallback: the editor came up on defaults and
            // the user's own settings were one unexplained launch from being overwritten.
            LOG_ERROR( "[Prefs] {} exists but could not be read: {} — this session runs on defaults, and "
                       "the next settings change will overwrite the file.",
                       PrefsFile(), raw.GetError() );
        }
        else if ( raw.GetValue().empty() )
        {
            LOG_WARN( "[Prefs] {} is empty; using defaults.", PrefsFile() );
        }
        // DefaultIfMissing: prefs written by older builds (fewer fields) keep loading — new
        // fields just take their in-struct defaults instead of failing the whole file.
        else if ( auto parsed = rfl::json::read<EditorPreferences, rfl::DefaultIfMissing>( raw.GetValue() );
                  parsed.has_value() )
        {
            Get() = parsed.value();
            // The canonical form of what the file holds, NOT the raw bytes: an older build's key order or
            // spacing is not a settings change, and a memo taken from the raw text would report the whole
            // struct as changed on the first save after an upgrade.
            s_OnDisk = rfl::json::write( Get() );
        }
        else
        {
            LOG_WARN( "[Prefs] editor.json is corrupt, using defaults: {}", parsed.error().what() );
        }

        if ( const auto raised = MigrateLoaded( Get() ); !raised.empty() )
        {
            // Contract §4.7: a migration says which file, from what to what, and how many fields moved.
            for ( const std::string& line : raised )
                LOG_INFO( "[Prefs] {} migrated: {}", PrefsFile(), line );

            // Written back in the new form so the migration runs once, not every launch — and through
            // SaveMigrated rather than Save, so the log names the migration instead of reporting a
            // settings change the user did not make.
            SaveMigrated( "migration write-back (" + std::to_string( raised.size() ) + " field(s) raised)" );
        }

        // CARRYING A KEY WE DO NOT UNDERSTAND IS AN EVENT, NOT A DETAIL. It means another build — an
        // agent's worktree, an older install, a branch that has since landed — owns settings this binary
        // cannot show or edit, and the only symptom otherwise available is the one К9 came from: nobody
        // noticing until the values were already gone. Named rather than counted, because "3 unknown
        // keys" tells a reader nothing about whether to go and look for the build that wrote them.
        if ( !Get().UnknownKeys.empty() )
        {
            std::string names = Get().UnknownKeys.begin()->first;
            for ( auto it = std::next( Get().UnknownKeys.begin() ); it != Get().UnknownKeys.end(); ++it )
                names += ", " + it->first;

            LOG_INFO( "[Prefs] {} holds {} key(s) this build does not know ({}); they belong to another "
                      "build and are preserved on save, not dropped.",
                      PrefsFile(), Get().UnknownKeys.size(), names );
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
        // Already in the requested state. Since К8 this is no longer what keeps the file still — Save()
        // compares the bytes and would skip the write anyway — but a section header reports a click on
        // every frame it is hovered-and-pressed, so returning here also saves serializing the whole
        // struct to find out that nothing moved.
        if ( collapsed == ( it != v.end() ) )
            return;
        if ( collapsed )
            v.push_back( name );
        else
            v.erase( it );
        Save();
    }

    bool EditorPreferences::Save()
    {
        return PersistCurrent( {} );
    }

    bool EditorPreferences::SaveMigrated( const std::string& what )
    {
        return PersistCurrent( what );
    }
} // namespace Desert::Editor
