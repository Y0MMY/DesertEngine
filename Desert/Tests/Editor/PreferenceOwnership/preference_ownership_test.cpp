// SAVING SETTINGS MUST NOT CHANGE A FIELD THE USER DID NOT TOUCH IN THAT ACTION.
//
// The statement is deliberately wider than the defect that produced it (К6), because the defect was a
// SHAPE and not a typo: one value, two stores, and writers that disagreed about which one was the
// authority. In the exact form it shipped:
//
//   * the four gizmo snap values existed twice — as fields of EditorPreferences (which is what
//     ~/.desertengine/editor.json holds) and as private statics of Core::GizmoState;
//   * the toolbar magnet and the viewport's snap popup wrote the GizmoState copy and never called
//     Save(), so a chosen step did not survive a restart;
//   * EditorPreferences::Save() pushed the OTHER copy back over it as its first statement, so any
//     unrelated save — the View menu's Perf HUD toggle, an MSAA pick in Scene Settings, a star on a
//     field in Details — silently reverted the step in the middle of a session, with no message.
//
// The fix is not a fourth Save() call. The second store is gone: EditorPreferences owns the four
// fields, GizmoState reads and writes them and keeps nothing, and Save() has no state to apply. These
// tests hold that, and the first three of them are red on the tree that shipped the defect.
//
// WHY THE ASSERTIONS ARE ABOUT A RELATION rather than about either side (desert-engine-verify §4): both
// sides were individually correct. GizmoState returned exactly what had last been written to it, and
// Save() wrote exactly what the struct held. A unit test of either passes. What was wrong was that they
// had to agree and nothing made them.
//
// THE SUITE WRITES A REAL editor.json, in a REAL home directory — see main(), which points HOME at a
// temporary one first. That is the point: half of what is asserted here is about what reaches the file
// and comes back, and a mock of the store cannot answer that.

#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/GizmoState.hpp>

// glm::vec3 <-> JSON reflector (OutlineColor). Must be visible before rfl::json, exactly as it must be
// in EditorPreferences.cpp — without it the whole struct is "Unsupported type" at the first vec3.
#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/fields.hpp>
#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using Desert::Editor::EditorPreferences;
using Gizmo = Desert::Editor::Core::GizmoState;

namespace
{
    std::string PrefsPath()
    {
        return EditorPreferences::ConfigDirectory() + "/editor.json";
    }

    // A freshly installed editor: nothing on disk, defaults in memory. Every test starts here so none
    // of them can pass on a value another one happened to leave behind.
    void FreshInstall()
    {
        std::error_code ec;
        std::filesystem::remove( PrefsPath(), ec );
        EditorPreferences::Get() = EditorPreferences{};
    }

    // Which fields of editor.json differ between two snapshots of it.
    //
    // The key list comes from rfl::fields<EditorPreferences>() — the same call rfl::json::write makes
    // in Save() — so this is field-count-proof by construction: a preference added tomorrow is compared
    // without anybody editing this file, which is the property a hand-written list cannot have and the
    // reason ConfigOwnership enumerates the same way.
    std::vector<std::string> FieldsThatDiffer( const std::string& before, const std::string& after )
    {
        const auto lhs = rfl::json::read<rfl::Generic>( before );
        const auto rhs = rfl::json::read<rfl::Generic>( after );
        EXPECT_TRUE( lhs.has_value() ) << "the 'before' snapshot is not readable JSON";
        EXPECT_TRUE( rhs.has_value() ) << "the 'after' snapshot is not readable JSON";
        if ( !lhs.has_value() || !rhs.has_value() )
            return { "<unreadable>" };

        const auto lhsObject = lhs.value().to_object();
        const auto rhsObject = rhs.value().to_object();
        if ( !lhsObject.has_value() || !rhsObject.has_value() )
            return { "<not-an-object>" };

        std::vector<std::string> differing;
        for ( const auto& meta : rfl::fields<EditorPreferences>() )
        {
            const std::string key = meta.name();
            const auto        a   = lhsObject.value().get( key );
            const auto        b   = rhsObject.value().get( key );
            if ( !a.has_value() || !b.has_value() )
            {
                differing.push_back( key + " <missing>" );
                continue;
            }
            if ( rfl::json::write( a.value() ) != rfl::json::write( b.value() ) )
                differing.push_back( key );
        }
        return differing;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// 1. ONE VALUE, ONE STORAGE
// ---------------------------------------------------------------------------------------------------

// The relation itself, asserted in BOTH directions, because a one-way push is exactly what the broken
// version had: Save() copied preferences -> GizmoState and nothing ever came back.
TEST( PreferenceOwnership, TheGizmoAndThePreferencesCannotHoldDifferentSnapValues )
{
    FreshInstall();

    // Written the way a toolbar writes it.
    Gizmo::SetTranslateSnap( 25.0f );
    Gizmo::SetRotateSnapDegrees( 45.0f );
    Gizmo::SetScaleSnap( 0.25f );
    Gizmo::SetPersistentSnap( true );

    EXPECT_FLOAT_EQ( EditorPreferences::Get().TranslateSnap, 25.0f );
    EXPECT_FLOAT_EQ( EditorPreferences::Get().RotateSnapDeg, 45.0f );
    EXPECT_FLOAT_EQ( EditorPreferences::Get().ScaleSnap, 0.25f );
    EXPECT_TRUE( EditorPreferences::Get().PersistentSnap );

    // Written the way the Preferences window writes it — straight into the owning fields.
    EditorPreferences::Get().TranslateSnap  = 10.0f;
    EditorPreferences::Get().RotateSnapDeg  = 90.0f;
    EditorPreferences::Get().ScaleSnap      = 0.5f;
    EditorPreferences::Get().PersistentSnap = false;

    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 10.0f );
    EXPECT_FLOAT_EQ( Gizmo::RotateSnapDegrees(), 90.0f );
    EXPECT_FLOAT_EQ( Gizmo::ScaleSnap(), 0.5f );
    EXPECT_FALSE( Gizmo::PersistentSnap() );
}

// SnapActive is the one piece of policy GizmoState still owns, and it reads the toggle it no longer
// stores. Ctrl INVERTS the toggle rather than forcing snapping on, which is the part that a rewrite of
// the accessor would be most likely to get backwards.
TEST( PreferenceOwnership, CtrlInvertsThePersistentToggleWhicheverWayItIsSet )
{
    FreshInstall();

    Gizmo::SetPersistentSnap( false );
    EXPECT_FALSE( Gizmo::SnapActive( false ) );
    EXPECT_TRUE( Gizmo::SnapActive( true ) );

    Gizmo::SetPersistentSnap( true );
    EXPECT_TRUE( Gizmo::SnapActive( false ) );
    EXPECT_FALSE( Gizmo::SnapActive( true ) );
}

// ---------------------------------------------------------------------------------------------------
// 2. THE HEADLINE RELATION
// ---------------------------------------------------------------------------------------------------

// The user's own sentence: set a snap step, then do something unrelated in another panel, and the step
// is still what you set. The unrelated action is the View menu's Perf HUD item, copied verbatim from
// EditorLayer::DrawViewMenu — one bool and a Save() — because that is the cheapest real trigger and
// nothing about it mentions the gizmo.
TEST( PreferenceOwnership, AnUnrelatedSaveChangesNoFieldTheUserDidNotTouch )
{
    FreshInstall();

    Gizmo::SetTranslateSnap( 25.0f );
    Gizmo::SetRotateSnapDegrees( 45.0f );
    Gizmo::SetScaleSnap( 0.25f );
    Gizmo::SetPersistentSnap( true );

    const std::string before = rfl::json::write( EditorPreferences::Get() );

    EditorPreferences::Get().ShowPerfHud = !EditorPreferences::Get().ShowPerfHud;
    EditorPreferences::Save();

    const std::vector<std::string> expected = { "ShowPerfHud" };
    EXPECT_EQ( FieldsThatDiffer( before, rfl::json::write( EditorPreferences::Get() ) ), expected )
         << "a save moved a field the action that triggered it never mentioned";

    // And the same statement asked of the thing the user can actually see — the step the gizmo snaps
    // by. This is the assertion the broken tree fails: the struct above was never touched by the old
    // Save() either; what it reverted was the SECOND copy, which is what the gizmo read.
    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 25.0f );
    EXPECT_FLOAT_EQ( Gizmo::RotateSnapDegrees(), 45.0f );
    EXPECT_FLOAT_EQ( Gizmo::ScaleSnap(), 0.25f );
    EXPECT_TRUE( Gizmo::PersistentSnap() );
}

// The same sentence with the OTHER real trigger, and one that is a preference in its own right: the
// MSAA combo in Scene Settings writes prefs.MSAASamples and saves. It is worth its own case because
// MSAA is the one value Save() still pushes anywhere (into Graphic::RenderConfig), so if a push were
// ever going to leak back into a neighbour, this is the action that would do it.
TEST( PreferenceOwnership, PickingMSAADoesNotDisturbTheSnapStep )
{
    FreshInstall();

    Gizmo::SetTranslateSnap( 500.0f );
    const std::string before = rfl::json::write( EditorPreferences::Get() );

    EditorPreferences::Get().MSAASamples = 4;
    EditorPreferences::Save();

    const std::vector<std::string> expected = { "MSAASamples" };
    EXPECT_EQ( FieldsThatDiffer( before, rfl::json::write( EditorPreferences::Get() ) ), expected );
    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 500.0f );
}

// The general form, stated over the whole struct so that a preference added tomorrow is covered without
// anyone remembering this file. Save() is allowed to write the file and nothing else; if it ever grows
// a second "apply" statement, this is what refuses it.
//
// The values are moved off their defaults on purpose: a struct left at its defaults cannot tell a
// harmless no-op from a reset, which is precisely how the original push went unnoticed.
TEST( PreferenceOwnership, SaveRewritesNothingInTheStructItWrites )
{
    FreshInstall();

    EditorPreferences::Get().TranslateSnap  = 7.0f;
    EditorPreferences::Get().RotateSnapDeg  = 3.0f;
    EditorPreferences::Get().ScaleSnap      = 0.75f;
    EditorPreferences::Get().PersistentSnap = true;
    EditorPreferences::Get().MSAASamples    = 8;
    EditorPreferences::Get().CameraSpeed    = 4.25f;
    EditorPreferences::Get().ShowPerfHud    = true;

    const std::string before = rfl::json::write( EditorPreferences::Get() );
    EditorPreferences::Save();

    EXPECT_EQ( FieldsThatDiffer( before, rfl::json::write( EditorPreferences::Get() ) ),
               std::vector<std::string>{} );
}

// ---------------------------------------------------------------------------------------------------
// 3. THE CHOICE REACHES THE FILE
// ---------------------------------------------------------------------------------------------------

// The small half of the defect: a step picked from the toolbar was live for the session and gone on the
// next launch, because the writer never reached the store. A restart is simulated exactly the way one
// happens — the process's copy is discarded and rebuilt by Load() from what is on disk.
TEST( PreferenceOwnership, AStepChosenFromAToolbarSurvivesARestart )
{
    FreshInstall();

    Gizmo::SetTranslateSnap( 25.0f );
    Gizmo::SetRotateSnapDegrees( 30.0f );
    Gizmo::SetScaleSnap( 0.5f );
    Gizmo::SetPersistentSnap( true );

    ASSERT_TRUE( std::filesystem::exists( PrefsPath() ) )
         << "no toolbar write reached " << PrefsPath() << " at all";

    EditorPreferences::Get() = EditorPreferences{}; // the next launch starts from the struct's defaults
    EditorPreferences::Load();

    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 25.0f );
    EXPECT_FLOAT_EQ( Gizmo::RotateSnapDegrees(), 30.0f );
    EXPECT_FLOAT_EQ( Gizmo::ScaleSnap(), 0.5f );
    EXPECT_TRUE( Gizmo::PersistentSnap() );
}

// Persisting on the click is only affordable because re-picking the step you are already on writes
// nothing. ImGui::Selectable reports a click whether or not it changed anything, and the magnet popup
// is a list of seven steps with the current one highlighted — so the commonest interaction with it is
// picking the value that is already set.
TEST( PreferenceOwnership, ReChoosingTheStepAlreadySetDoesNotRewriteTheFile )
{
    FreshInstall();

    Gizmo::SetTranslateSnap( 25.0f );
    ASSERT_TRUE( std::filesystem::exists( PrefsPath() ) );

    std::error_code ec;
    std::filesystem::remove( PrefsPath(), ec );

    Gizmo::SetTranslateSnap( 25.0f );  // the same step, picked again
    Gizmo::SetPersistentSnap( false ); // the toggle, set to what it already is

    EXPECT_FALSE( std::filesystem::exists( PrefsPath() ) )
         << "a choice that changed nothing rewrote editor.json (and logged a save that did nothing)";

    // ...and a real change still writes.
    Gizmo::SetTranslateSnap( 50.0f );
    EXPECT_TRUE( std::filesystem::exists( PrefsPath() ) );
}

int main( int argc, char** argv )
{
    // ~/.desertengine/editor.json is a real file in a real home directory, and this suite writes it. Point
    // HOME at a temporary directory before anything can read it, or a test run would overwrite the
    // developer's own editor preferences. ProjectContext::ConfigDirectory() reads HOME on every call and
    // caches nothing, and it is the variable it consults first on Windows too, so this is enough.
    const std::filesystem::path home = std::filesystem::temp_directory_path() / "DesertPreferenceOwnership";

    std::error_code ec;
    std::filesystem::remove_all( home, ec );
    std::filesystem::create_directories( home, ec );

#ifdef DESERT_PLATFORM_WINDOWS
    _putenv_s( "HOME", home.string().c_str() );
#else
    setenv( "HOME", home.string().c_str(), 1 );
#endif

    ::testing::InitGoogleTest( &argc, argv );
    const int result = RUN_ALL_TESTS();

    std::filesystem::remove_all( home, ec );
    return result;
}
