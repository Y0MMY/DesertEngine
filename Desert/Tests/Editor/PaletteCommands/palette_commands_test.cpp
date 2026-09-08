// A COMMAND THAT FAILED MUST NOT ANSWER LIKE ONE THAT WORKED.
//
// `PaletteCommand::Run` returned `void`. The palette's dictionary is also the control channel's
// vocabulary (Г14), so `run` over the socket answered `{"ok":true}` for a command that had FAILED — a
// document that would not resolve, a scene that would not save, an Apply that published nothing — and the
// only trace was a line in a log the client was not reading. That is the Ф4/Г13 shape this codebase spent
// a day removing: a result that EXISTS, is KNOWN, and is thrown away at the boundary. `SetData` on the
// buffers was the same defect one layer down.
//
// Measured on the live editor before the fix: `desertctl run Action Undo` against an empty history exited
// 0 with `{"ok":true}`, having undone nothing.
//
// WHAT IS ASSERTED HERE, and why each is a relation rather than a spot value:
//
//   1. THE TWO SPELLINGS OF SUCCESS ARE DISTINGUISHABLE IN THE SOURCE AND IDENTICAL ON THE WIRE.
//      `PaletteCommandDone()` marks an entry with no failure mode; `PaletteCommandOutcome(moved, why)`
//      carries an answer something actually gave back. A reader can grep which is which; a client cannot
//      tell them apart, and must not be able to — "it worked" is one fact.
//   2. A FALSE ALWAYS CARRIES WORDS. A refusal with nothing said is the shape the whole channel exists to
//      make impossible, and `bool`-returning editor operations are exactly where it would creep back in.
//   3. THE OUTCOME SURVIVES THE DICTIONARY. Resolution and execution are two steps over one list, and a
//      failure has to reach the caller THROUGH them — this is the property EditorLayer's `Op::Run` and
//      CommandPalette::Draw both depend on and neither can assert (EditorLayer.cpp and CommandPalette.cpp
//      are compiled by no suite: scripts/CI/UnreachedSources.sh).

#include <Editor/Core/CommandPalette.hpp>
#include <Editor/Core/Control/ControlDispatch.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Desert::Editor::PaletteCommand;
using Desert::Editor::PaletteCommandDone;
using Desert::Editor::PaletteCommandOutcome;
using Desert::Editor::Control::CommandAddress;
using Desert::Editor::Control::ResolveCommand;

// ---------------------------------------------------------------------------------------------------
// 1 & 2. The outcome protocol.
// ---------------------------------------------------------------------------------------------------

TEST( PaletteCommands, AnEntryWithNoFailureModeSucceeds )
{
    EXPECT_TRUE( PaletteCommandDone().IsSuccess() );
}

// THE `bool` HALF, which is where the defect lived: three ISubjectDocument operations and the scene save
// answer "did anything move" and nothing else. True is a success; FALSE MUST CARRY WORDS, because "the
// document declined" is all the editor knows and a client told nothing at all would read it as done.
TEST( PaletteCommands, AFalseOutcomeCarriesTheReasonItWasGiven )
{
    const auto refused = PaletteCommandOutcome( false, "the scene was NOT saved." );

    ASSERT_FALSE( refused.IsSuccess() );
    EXPECT_EQ( refused.GetError(), "the scene was NOT saved." );
}

TEST( PaletteCommands, ATrueOutcomeIsASuccessAndSaysNothingElse )
{
    EXPECT_TRUE( PaletteCommandOutcome( true, "unused" ).IsSuccess() );
}

// THE TWO SPELLINGS OF SUCCESS ARE ONE FACT ON THE WIRE. They exist so a READER can tell "this cannot
// fail" from "this was asked and said yes"; a client must not be able to, or "ok" would mean two things.
TEST( PaletteCommands, BothSpellingsOfSuccessAreIndistinguishableToACaller )
{
    const auto cannotFail = PaletteCommandDone();
    const auto didWork    = PaletteCommandOutcome( true, "unused" );

    EXPECT_EQ( cannotFail.IsSuccess(), didWork.IsSuccess() );
    EXPECT_TRUE( cannotFail.IsSuccess() );
}

// A REFUSAL IS NEVER EMPTY. Asserted as a property over the messages the editor actually passes in,
// rather than one of them, because the failure this guards is somebody adding a fifth call site and
// leaving the string blank — which produces a refusal a client can see and not act on.
TEST( PaletteCommands, NoRefusalIsEverWordless )
{
    for ( const char* reason :
          { "there was nothing left to undo.", "the document published nothing: it had no outstanding edit.",
            "the scene was NOT saved; the log line above says why." } )
    {
        const auto refused = PaletteCommandOutcome( false, reason );
        ASSERT_FALSE( refused.IsSuccess() );
        EXPECT_FALSE( refused.GetError().empty() );
    }
}

// ---------------------------------------------------------------------------------------------------
// 3. The outcome survives the dictionary.
// ---------------------------------------------------------------------------------------------------

namespace
{
    // A dictionary shaped like the editor's: entries that cannot fail beside entries that can, which is
    // the mixture both consumers walk.
    std::vector<PaletteCommand> Dictionary( bool saveWillWork )
    {
        std::vector<PaletteCommand> commands;
        commands.push_back( { "View", "Toggle the grid", [] { return PaletteCommandDone(); } } );
        commands.push_back( { "Action", "Undo",
                              [] { return PaletteCommandOutcome( false, "there was nothing left to undo." ); } } );
        commands.push_back( { "Action", "Save Scene", [saveWillWork]
                              { return PaletteCommandOutcome( saveWillWork, "the scene was NOT saved." ); } } );
        return commands;
    }
} // namespace

// THE RELATION EditorLayer's Op::Run RESTS ON: one dictionary, resolved against and run out of, with the
// failure reaching the caller through both steps. It used to be resolved, run, and answered `ok`.
TEST( PaletteCommands, AResolvedEntryThatRefusesReachesTheCallerAsARefusal )
{
    const std::vector<PaletteCommand> dictionary = Dictionary( /*saveWillWork=*/false );

    const auto resolved = ResolveCommand( dictionary, CommandAddress{ "Action", "Undo" } );
    ASSERT_TRUE( resolved.Found );

    const auto ran = dictionary[resolved.Index].Run();
    ASSERT_FALSE( ran.IsSuccess() ) << "the entry refused and the dictionary reported success";
    EXPECT_NE( ran.GetError().find( "nothing left to undo" ), std::string::npos );
}

// ...and the other direction, which is the one an over-eager change breaks: an entry that WORKED must
// still come back as success through the same two steps.
TEST( PaletteCommands, AResolvedEntryThatWorksStillSucceeds )
{
    const std::vector<PaletteCommand> dictionary = Dictionary( /*saveWillWork=*/true );

    for ( const CommandAddress& wanted :
          { CommandAddress{ "View", "Toggle the grid" }, CommandAddress{ "Action", "Save Scene" } } )
    {
        const auto resolved = ResolveCommand( dictionary, wanted );
        ASSERT_TRUE( resolved.Found ) << wanted.Group << " / " << wanted.Label;
        EXPECT_TRUE( dictionary[resolved.Index].Run().IsSuccess() ) << wanted.Group << " / " << wanted.Label;
    }
}

// ONE ENTRY, ONE RUN. The dictionary is rebuilt per request, so a command that is resolved must be the
// command that runs — asserted by counting, because "it ran the right one" and "it ran it once" are two
// different claims and a loop that fell through would satisfy the first.
TEST( PaletteCommands, ResolvingAndRunningTouchesExactlyTheOneEntry )
{
    int grid = 0;
    int undo = 0;

    std::vector<PaletteCommand> dictionary;
    dictionary.push_back( { "View", "Toggle the grid", [&grid]
                            {
                                ++grid;
                                return PaletteCommandDone();
                            } } );
    dictionary.push_back( { "Action", "Undo", [&undo]
                            {
                                ++undo;
                                return PaletteCommandOutcome( false, "nothing to undo." );
                            } } );

    const auto resolved = ResolveCommand( dictionary, CommandAddress{ "Action", "Undo" } );
    ASSERT_TRUE( resolved.Found );
    EXPECT_FALSE( dictionary[resolved.Index].Run().IsSuccess() );

    EXPECT_EQ( undo, 1 );
    EXPECT_EQ( grid, 0 ) << "resolving one entry ran another";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
