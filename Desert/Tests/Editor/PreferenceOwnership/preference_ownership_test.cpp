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
//
// ---------------------------------------------------------------------------------------------------
// К8 EXTENDED IT WITH THE OTHER HALF OF THE SAME SHAPE, and the two are worth stating side by side:
//
//   К6: one VALUE had two owners  — the snap step lived in EditorPreferences and in GizmoState, and the
//       two writers disagreed about which was the authority.
//   К8: one DECISION had two takers — the Preferences window's "Save" button claimed to decide whether
//       an edit was kept, but every control in that window edits the LIVE store, so the edit was already
//       in force before the button was reached. Closing the window with the x left the change working
//       and unwritten ("I cancelled" — it did not); and any unrelated save from another panel (the Perf
//       HUD toggle, an MSAA pick, a star in Details) silently committed those abandoned edits to disk.
//       The button promised a decision it did not take, and a stranger in another panel took it.
//
// The button is gone. Every control commits itself on ImGui::IsItemDeactivatedAfterEdit() — one file
// write when the mouse is released — and Save() skips a write whose bytes match what is already on disk,
// which is what makes committing on every control affordable.
//
// TWO KINDS OF ASSERTION FOLLOW, and neither covers the other. Sections 1-4 link the real store and ask
// it questions. Section 5 READS EditorLayer::DrawPreferencesWindow's source text, because the half of
// the statement that says "exactly when the user let go" lives in a function this suite cannot link (it
// needs ImGui, the renderer and the whole editor) and is not observable from the store at all — a store
// cannot tell whether the call that reached it came from a control's release or from a button. Reading
// the source for a census has precedent here: Desert/Tests/Engine/DeviceLostCensus does the same thing
// for the same reason, and shares the reader this file includes.

#include <Editor/Core/EditorPreferences.hpp>
#include <Editor/Core/GizmoState.hpp>

// glm::vec3 <-> JSON reflector (OutlineColor). Must be visible before rfl::json, exactly as it must be
// in EditorPreferences.cpp — without it the whole struct is "Unsupported type" at the first vec3.
#include <Common/Core/Serialization/GlmReflection.hpp>

// The shared source reader (Д33): comments and literals blanked, length and line breaks preserved. §5
// needs it for the same reason DeviceLostCensus does — a census that counted the calls named in comments
// would certify prose, and DrawPreferencesWindow is now mostly prose.
#include "../../Engine/SettingConsumers/setting_consumers_reader.hpp"

#include <rflcpp/rfl/Generic.hpp>
#include <rflcpp/rfl/fields.hpp>
#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    std::string ReadWholeFile( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    void WriteWholeFile( const std::filesystem::path& path, const std::string& text )
    {
        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        out << text;
    }

    // A freshly installed editor: nothing on disk, defaults in memory. Every test starts here so none
    // of them can pass on a value another one happened to leave behind.
    //
    // THE Load() CALL IS LOAD-BEARING and was added by К8. The store remembers what it believes is on
    // disk so that it can skip a write that would change nothing, and that memo is process-wide — it
    // outlives a test the way the file does not. Load() with no file present is how a real first run
    // tells the store the disk is empty, so it is also how a test does: without it, a test whose values
    // happened to match the previous test's would find its first save skipped.
    void FreshInstall()
    {
        std::error_code ec;
        std::filesystem::remove( PrefsPath(), ec );
        EditorPreferences::Get() = EditorPreferences{};
        EditorPreferences::Load();
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
//
// THE PROBE IS A MARKER, NOT A DELETION, and К8 changed it for a reason worth keeping: the old version
// removed editor.json and asserted it did not come back, which stopped being the right question once the
// store learned to confirm the file is still there before skipping a write. Deleting the file behind a
// running editor SHOULD produce a write — a "true" that meant "my memo says so" while nothing was on
// disk would be §1.4's silent wrong answer. Overwriting the file with a marker asks the question the
// test is actually about: does a redundant save touch the bytes?
TEST( PreferenceOwnership, ReChoosingTheStepAlreadySetDoesNotRewriteTheFile )
{
    FreshInstall();

    Gizmo::SetTranslateSnap( 25.0f );
    ASSERT_TRUE( std::filesystem::exists( PrefsPath() ) );

    const std::string marker = "{ \"this file was not rewritten\": true }";
    WriteWholeFile( PrefsPath(), marker );

    Gizmo::SetTranslateSnap( 25.0f );  // the same step, picked again
    Gizmo::SetPersistentSnap( false ); // the toggle, set to what it already is

    EXPECT_EQ( ReadWholeFile( PrefsPath() ), marker )
         << "a choice that changed nothing rewrote editor.json (and logged a save that did nothing)";

    // ...and a real change still writes.
    Gizmo::SetTranslateSnap( 50.0f );
    EXPECT_NE( ReadWholeFile( PrefsPath() ), marker );
}

// ---------------------------------------------------------------------------------------------------
// 4. THE ONE MIGRATION THIS FILE CARRIES
// ---------------------------------------------------------------------------------------------------

// Load() raises a metre-era TranslateSnap to centimetres once and writes it back, and until now nothing
// tested it — which matters more than it looks, because it is the only code in the editor that can
// change a preference the user did not touch in that session, and it is therefore the one legitimate
// exception to this suite's headline. It earns the exception by being a MIGRATION: it fires on a value
// this build cannot produce, it says so in the log, and it persists the new form so it never fires
// again. Contract §4.4 asks a migration to be tested; this is that test.
TEST( PreferenceOwnership, AMetreEraSnapIsRaisedToCentimetresOnceAndWrittenBack )
{
    FreshInstall();

    // 0.5 "world units" from when a unit was a metre — half a metre, meant as 50 cm.
    EditorPreferences::Get().TranslateSnap = 0.5f;
    ASSERT_TRUE( EditorPreferences::Save() );

    EditorPreferences::Get() = EditorPreferences{};
    EditorPreferences::Load();
    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 50.0f );

    // Written back in the new form, so the SECOND launch reads 50 and the migration does not fire on it
    // again — a migration that ran every launch would multiply by a hundred every time.
    EditorPreferences::Get() = EditorPreferences{};
    EditorPreferences::Load();
    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 50.0f );
}

// The other half, and the one that says the rule is not "anything small is metres": 1 cm is the
// smallest step the editor offers, so the migration's window is strictly below it and a legitimate
// centimetre value must pass through untouched.
TEST( PreferenceOwnership, ACentimetreEraSnapIsLeftAlone )
{
    FreshInstall();

    EditorPreferences::Get().TranslateSnap = 1.0f;
    ASSERT_TRUE( EditorPreferences::Save() );

    EditorPreferences::Get() = EditorPreferences{};
    EditorPreferences::Load();
    EXPECT_FLOAT_EQ( Gizmo::TranslateSnap(), 1.0f );
}

// ---------------------------------------------------------------------------------------------------
// 5. К8 — ONE WRITE PER FINISHED EDIT, AND NOT ONE MORE
// ---------------------------------------------------------------------------------------------------

// The user's sentence, in the half a linked test can answer: a finished edit reaches the file once, and
// a save with nothing new in it does not touch the file at all.
//
// This is red on the tree that shipped the defect in the second direction — Save() wrote and logged
// unconditionally, so every redundant call was a file write and a "[Prefs] Saved ..." line. That was
// affordable while one button was the only writer in this window; it is not affordable now that nine
// controls each commit on release and three other panels commit on a click.
//
// The 60 frames are the drag itself: a DragFloat accumulates into the value it is handed and reports a
// change on every frame the mouse moves, which is why the commit waits for the release instead of riding
// the change. That the release is where the window calls Save() is §6's business, not this test's — a
// store cannot see which call site reached it.
TEST( PreferenceOwnership, AFinishedEditReachesTheFileOnceAndARedundantSaveNotAtAll )
{
    FreshInstall();

    EditorPreferences::Get().OutlineWidth = 4.0f;
    ASSERT_TRUE( EditorPreferences::Save() );
    const std::string beforeDrag = ReadWholeFile( PrefsPath() );

    for ( int frame = 0; frame < 60; ++frame )
        EditorPreferences::Get().OutlineWidth = 4.0f + 0.1f * static_cast<float>( frame );

    ASSERT_TRUE( EditorPreferences::Save() ); // the release
    const std::string afterDrag = ReadWholeFile( PrefsPath() );

    const std::vector<std::string> expected = { "OutlineWidth" };
    EXPECT_EQ( FieldsThatDiffer( beforeDrag, afterDrag ), expected )
         << "the release wrote more than the control the user was holding";

    // A second release with nothing new — letting go of a control you only hovered, or putting a handle
    // back where you found it. A marker appended to the file survives iff no write happened.
    const std::string marked = afterDrag + "\n";
    WriteWholeFile( PrefsPath(), marked );
    EXPECT_TRUE( EditorPreferences::Save() );
    EXPECT_EQ( ReadWholeFile( PrefsPath() ), marked )
         << "a save with nothing new in it rewrote editor.json";
}

// The same statement asked of the FILE rather than of the struct, which is where К8's second symptom
// lived: a stranger's save is what carried the abandoned edits to disk, so what the file gains when an
// unrelated panel saves is the thing to pin. `AnUnrelatedSaveChangesNoFieldTheUserDidNotTouch` above
// asks this of the in-memory struct and cannot see a write at all.
TEST( PreferenceOwnership, TheFileGainsOnlyTheFieldTheSavingActionTouched )
{
    FreshInstall();

    // A Preferences control, committed by its own release the way every control in that window now is.
    EditorPreferences::Get().CameraSpeed = 6.0f;
    ASSERT_TRUE( EditorPreferences::Save() );
    const std::string before = ReadWholeFile( PrefsPath() );

    // Somebody toggles the Perf HUD from the View menu — a different panel, one bool, one save.
    EditorPreferences::Get().ShowPerfHud = !EditorPreferences::Get().ShowPerfHud;
    ASSERT_TRUE( EditorPreferences::Save() );

    const std::vector<std::string> expected = { "ShowPerfHud" };
    EXPECT_EQ( FieldsThatDiffer( before, ReadWholeFile( PrefsPath() ) ), expected );
}

// A save that could not be written must not leave the store believing the disk agrees with it — the memo
// that lets a redundant save be skipped is only safe while it is updated on success alone. Provoked the
// one way that needs no privileges: a DIRECTORY sitting where editor.json's temporary file must go, so
// the atomic write's rename cannot succeed.
TEST( PreferenceOwnership, AFailedWriteIsNotRememberedAsASuccessfulOne )
{
    FreshInstall();

    EditorPreferences::Get().CameraSpeed = 2.5f;
    ASSERT_TRUE( EditorPreferences::Save() );

    std::error_code ec;
    const std::filesystem::path blocker = PrefsPath() + ".tmp";
    std::filesystem::create_directory( blocker, ec );
    ASSERT_TRUE( std::filesystem::is_directory( blocker ) ) << "could not stage a blocked write";

    EditorPreferences::Get().CameraSpeed = 9.0f;
    EXPECT_FALSE( EditorPreferences::Save() ) << "a write that could not happen reported success";

    std::filesystem::remove_all( blocker, ec );

    // The retry must actually write. If the failed attempt had updated the memo, this save would be
    // skipped as redundant and the setting would be lost with nothing in the log to say so.
    ASSERT_TRUE( EditorPreferences::Save() );
    EditorPreferences::Get() = EditorPreferences{};
    EditorPreferences::Load();
    EXPECT_FLOAT_EQ( EditorPreferences::Get().CameraSpeed, 9.0f );
}

// Load() writes in exactly one case — a migration — and the migration itself is a pure function of the
// struct (contract §4.4). Asserted directly, with no file anywhere near it, which is the whole point of
// extracting it: the old code did the raise inline in Load() and called Save() from it, so the only way
// to test the arithmetic was to write and re-read a file.
TEST( PreferenceOwnership, TheMigrationIsAPureFunctionAndNamesWhatItRaised )
{
    EditorPreferences metreEra;
    metreEra.TranslateSnap = 0.5f;

    const auto raised = EditorPreferences::MigrateLoaded( metreEra );
    ASSERT_EQ( raised.size(), 1u );
    EXPECT_NE( raised.front().find( "TranslateSnap" ), std::string::npos )
         << "a migration line that does not name the field it moved: " << raised.front();
    EXPECT_FLOAT_EQ( metreEra.TranslateSnap, 50.0f );

    // Idempotent: run on its own output it has nothing left to do. A migration that fired twice would
    // multiply by a hundred every launch.
    EXPECT_TRUE( EditorPreferences::MigrateLoaded( metreEra ).empty() );

    EditorPreferences centimetreEra;
    centimetreEra.TranslateSnap = 1.0f;
    EXPECT_TRUE( EditorPreferences::MigrateLoaded( centimetreEra ).empty() );
    EXPECT_FLOAT_EQ( centimetreEra.TranslateSnap, 1.0f );
}

// ---------------------------------------------------------------------------------------------------
// 6. К8 — THE CENSUS OF THE PREFERENCES WINDOW ITSELF
// ---------------------------------------------------------------------------------------------------
//
// "Exactly when the user let go of the mouse" is a statement about CALL SITES, and no amount of asking
// the store can answer it: the store sees a Save() and cannot tell a control's release from a button.
// So this section reads EditorLayer::DrawPreferencesWindow and asserts three things about its text —
// that every control in it commits, that nothing in it saves outside a commit (which is how the "Save"
// button is kept dead), and that every control really is bound to a field of the preference struct.
//
// It is red on the tree that shipped the defect in the first direction: nine controls, zero commits, one
// button. It is deliberately a census rather than a spot check, because the failure mode of nine
// copy-pasted guards is that the tenth control arrives without one — a middle link that silently drops a
// property, which is the recurring defect shape in this project and the reason the rule is enforced over
// the whole function instead of trusted per line.

namespace
{
    namespace Text = Desert::Tests::ConsumerText;

    // Walks up from the working directory looking for a file only the repository has. Same shape as
    // DeviceLostCensus's and AssetReferenceCensus's, and copied for the same reason they are copies of
    // each other: each census probes a DIFFERENT sentinel, so sharing it would mean passing the sentinel
    // in and would say less than the three lines it replaced.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Editor/Source/EditorLayer.cpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    // The body of `Class::Method`, by brace matching from the first '{' after the name. Empty when the
    // name is not there at all, which the callers report as a distinct failure: a census naming a
    // function that no longer exists is worse than a missing guard, because it passes.
    std::string BodyOf( const std::string& src, const std::string& qualifiedName )
    {
        std::size_t at = 0;
        while ( ( at = src.find( qualifiedName, at ) ) != std::string::npos )
        {
            const std::size_t open = src.find( '{', at );
            if ( open == std::string::npos )
                return {};
            const std::size_t semi = src.find( ';', at );
            if ( semi != std::string::npos && semi < open )
            {
                at += qualifiedName.size();
                continue;
            }
            int         depth = 0;
            std::size_t i     = open;
            for ( ; i < src.size(); ++i )
            {
                if ( src[i] == '{' )
                    ++depth;
                else if ( src[i] == '}' && --depth == 0 )
                    break;
            }
            return src.substr( open, i - open );
        }
        return {};
    }

    std::string PreferencesWindowBody()
    {
        const std::string root = RepoRoot();
        EXPECT_FALSE( root.empty() ) << "could not find the repository root from the working directory";
        const std::string src = Text::StripCommentsAndLiterals( ReadWholeFile( root + "Editor/Source/EditorLayer.cpp" ) );
        return BodyOf( src, "EditorLayer::DrawPreferencesWindow" );
    }

    // EVERY ImGui CALL THE WINDOW MAKES, split by whether it can edit a preference. The lists are the
    // census: a call in neither of them fails the suite with "classify this", which is what stops a new
    // control from being added without the question being asked. `Button` is deliberately in NEITHER —
    // the button that took this task's decision away is not a call this window is allowed to make again
    // without somebody arguing for it here first.
    const std::vector<std::string>& ControlCalls()
    {
        static const std::vector<std::string> calls = {
             "SliderFloat", "SliderFloat2", "SliderFloat3", "SliderInt",  "SliderAngle", "DragFloat",
             "DragFloat2",  "DragFloat3",   "DragInt",      "Checkbox",   "ColorEdit3",  "ColorEdit4",
             "InputText",   "InputInt",     "InputFloat",   "InputFloat3", "Combo",      "RadioButton" };
        return calls;
    }

    const std::vector<std::string>& InertCalls()
    {
        static const std::vector<std::string> calls = {
             "Begin",   "End",        "SetNextWindowSize",          "SetNextItemWidth",
             "Spacing", "Separator",  "SameLine",                   "NewLine",
             "Dummy",   "Text",       "TextUnformatted",            "TextDisabled",
             "PushID",  "PopID",      "Indent",                     "Unindent",
             "BeginDisabled",         "EndDisabled",                "IsItemHovered",
             "SetTooltip",            "IsItemDeactivatedAfterEdit", "PushItemWidth",
             "PopItemWidth" };
        return calls;
    }

    bool Contains( const std::vector<std::string>& haystack, const std::string& needle )
    {
        return std::find( haystack.begin(), haystack.end(), needle ) != haystack.end();
    }

    // Positions of `ImGui::<name>` for every name in `names`, in source order.
    std::vector<std::size_t> CallPositions( const std::string& body, const std::vector<std::string>& names )
    {
        std::vector<std::size_t> out;
        for ( const std::string& name : names )
            for ( std::size_t at : Text::WordPositions( body, name ) )
            {
                if ( at < 7 || body.compare( at - 7, 7, "ImGui::" ) != 0 )
                    continue;
                out.push_back( at );
            }
        std::sort( out.begin(), out.end() );
        return out;
    }

    // Every `ImGui::<name>` the body contains, whatever the name.
    std::vector<std::string> EveryImGuiCall( const std::string& body )
    {
        std::vector<std::string> out;
        for ( std::size_t at = body.find( "ImGui::" ); at != std::string::npos;
              at             = body.find( "ImGui::", at + 1 ) )
        {
            if ( at > 0 && Text::IsIdentChar( body[at - 1] ) )
                continue;
            const std::string name = Text::IdentAt( body, at + 7 );
            if ( !name.empty() && !Contains( out, name ) )
                out.push_back( name );
        }
        return out;
    }

    // The balanced argument list of the call whose name ends at `afterName`.
    std::string ArgumentsOf( const std::string& body, std::size_t afterName )
    {
        const std::size_t open = body.find( '(', afterName );
        if ( open == std::string::npos )
            return {};
        int         depth = 0;
        std::size_t i     = open;
        for ( ; i < body.size(); ++i )
        {
            if ( body[i] == '(' )
                ++depth;
            else if ( body[i] == ')' && --depth == 0 )
                break;
        }
        return body.substr( open, i - open );
    }
} // namespace

// The gate that keeps the other two honest: a control this file has never heard of is neither covered by
// the commit rule nor visible to the binding rule, so it must stop the suite rather than pass through it.
TEST( PreferenceOwnershipWindow, EveryCallInThePreferencesWindowIsClassified )
{
    const std::string body = PreferencesWindowBody();
    ASSERT_FALSE( body.empty() ) << "EditorLayer::DrawPreferencesWindow was not found in EditorLayer.cpp";

    for ( const std::string& name : EveryImGuiCall( body ) )
        EXPECT_TRUE( Contains( ControlCalls(), name ) || Contains( InertCalls(), name ) )
             << "ImGui::" << name
             << " is called in the Preferences window and this census does not know whether it can edit a "
                "preference. Add it to ControlCalls() (and give it a commit) or to InertCalls(). If it is "
                "ImGui::Button, read the comment above DrawPreferencesWindow first: К8 removed the one "
                "this window used to have.";
}

// THE HEADLINE. Every control commits, and it commits on the release rather than on the change.
TEST( PreferenceOwnershipWindow, EveryControlCommitsWhenTheUserLetsGoOfIt )
{
    const std::string body = PreferencesWindowBody();
    ASSERT_FALSE( body.empty() ) << "EditorLayer::DrawPreferencesWindow was not found in EditorLayer.cpp";

    const std::vector<std::size_t> controls = CallPositions( body, ControlCalls() );
    ASSERT_FALSE( controls.empty() ) << "the Preferences window draws no control at all";

    const std::vector<std::size_t> commits = Text::WordPositions( body, "IsItemDeactivatedAfterEdit" );

    for ( std::size_t i = 0; i < controls.size(); ++i )
    {
        const std::size_t from = controls[i];
        const std::size_t to   = i + 1 < controls.size() ? controls[i + 1] : body.size();

        bool committed = false;
        for ( std::size_t commit : commits )
            committed = committed || ( commit > from && commit < to );

        EXPECT_TRUE( committed ) << "the control at offset " << from
                                 << " of DrawPreferencesWindow is not followed by an "
                                    "ImGui::IsItemDeactivatedAfterEdit() commit before the next control: "
                                    "an edit made with it is live but never written, which is exactly the "
                                    "state К8 removed.";
    }
}

// The other half, and the one that keeps the button dead: nothing in this window may write the file
// except a commit. Stated as an interleaving rather than a count, so that a save moved away from its
// guard is caught as readily as a save with no guard at all.
TEST( PreferenceOwnershipWindow, NothingInThePreferencesWindowSavesOutsideACommit )
{
    const std::string body = PreferencesWindowBody();
    ASSERT_FALSE( body.empty() ) << "EditorLayer::DrawPreferencesWindow was not found in EditorLayer.cpp";

    const std::vector<std::size_t> commits = Text::WordPositions( body, "IsItemDeactivatedAfterEdit" );
    const std::vector<std::size_t> saves   = Text::WordPositions( body, "Save" );

    ASSERT_EQ( saves.size(), commits.size() )
         << "the Preferences window calls Save() " << saves.size() << " time(s) behind " << commits.size()
         << " commit(s). A save that is not a control's release is a second decider for the same "
            "decision — the shape К8 exists to remove.";

    for ( std::size_t i = 0; i < saves.size(); ++i )
        EXPECT_LT( commits[i], saves[i] ) << "a Save() at offset " << saves[i]
                                          << " of DrawPreferencesWindow does not sit behind its own commit";
}

// And every control really edits a PREFERENCE, not a local copy of one. The field names are checked
// against rfl::fields<EditorPreferences>() — the same list Save() serializes — so a field renamed in the
// struct and left behind in the window is caught here rather than by a setting that quietly stops
// persisting.
TEST( PreferenceOwnershipWindow, EveryControlIsBoundToARealPreferenceField )
{
    const std::string body = PreferencesWindowBody();
    ASSERT_FALSE( body.empty() ) << "EditorLayer::DrawPreferencesWindow was not found in EditorLayer.cpp";

    // The name the window binds the store to, derived rather than spelled: renaming the local is an
    // honest edit that changes nothing about whether the controls reach the store.
    const std::vector<std::size_t> gets = Text::WordPositions( body, "Get" );
    ASSERT_FALSE( gets.empty() ) << "the Preferences window never reaches EditorPreferences::Get()";
    const std::string store =
         Text::DeclaredNameBeforeAssignment( Text::StatementBefore( body, gets.front() ) );
    ASSERT_FALSE( store.empty() ) << "could not see what EditorPreferences::Get() is bound to";

    std::vector<std::string> fields;
    for ( const auto& meta : rfl::fields<EditorPreferences>() )
        fields.push_back( std::string( meta.name() ) );

    for ( std::size_t at : CallPositions( body, ControlCalls() ) )
    {
        const std::string args  = ArgumentsOf( body, at );
        int               bound = 0;
        for ( std::size_t use : Text::WordPositions( args, store ) )
        {
            const std::size_t dot = Text::SkipSpace( args, use + store.size() );
            if ( dot >= args.size() || args[dot] != '.' )
                continue;
            const std::string field = Text::IdentAt( args, Text::SkipSpace( args, dot + 1 ) );
            if ( field.empty() )
                continue;
            ++bound;
            EXPECT_TRUE( Contains( fields, field ) )
                 << "the Preferences window edits " << store << "." << field
                 << ", which is not a field of EditorPreferences";
        }
        EXPECT_GT( bound, 0 ) << "the control at offset " << at
                              << " of DrawPreferencesWindow edits nothing in the preference store";
    }
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
