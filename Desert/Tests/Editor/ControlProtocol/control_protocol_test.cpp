// A REQUEST IS UNDERSTOOD OR NAMED, AND A REPLY ALWAYS CARRIES AN OUTCOME.
//
// The control channel replaces a family of command-line flags that grew one entry per task, and it inherits
// the rule those flags were eventually forced to obey: EVERY input is either recognised and consumed, or
// refused with a message naming it. There is no third outcome. The flags learned it the expensive way --
// `--sceen X` parsed to "nothing happened", which for a capture meant a plausible PNG of the DEFAULT scene
// under the name of the one that was asked for, and one row of measurements was lost to it. A request
// dropped in silence is that same failure over a socket: the client concludes the editor did as it asked.
//
// The relations here:
//
//   1. TOTALITY. An operation this parser does not know is an error that names itself AND lists the known
//      ones -- built from the table the parser actually reads, so the two cannot drift.
//   2. INCOMPLETE REQUESTS ARE REFUSED. A `run` with no label, a shot with no path: each would otherwise
//      "succeed" having done nothing, or having written nowhere.
//   3. AN OUTCOME IS ALWAYS PRESENT, and a refusal always carries a reason. Response has no way to express
//      "failed, and nothing said" -- the shape this project keeps finding and removing.
//   4. ROUND TRIP. What a request means survives being written and read back. This is what says the wire
//      is a wire and not two programs agreeing by luck.
//   5. THE TWO CAPTURES ARE TWO OPERATIONS and neither can be mistaken for the other. Until this channel
//      existed no picture this engine took contained one pixel of its interface, so a window shot quietly
//      answered by a viewport shot would be a picture of the wrong subject under the right name.
//   6. STATE SECTIONS. An unknown section is refused rather than omitted: omitted, it comes back empty,
//      which reads exactly like a section that exists and is empty. One of those two readings is a lie.

#include <Editor/Core/Control/ControlProtocol.hpp>
#include <Editor/Core/Control/ControlState.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using Desert::Editor::Control::EditorQuiescence;
using Desert::Editor::Control::EditorSnapshot;
using Desert::Editor::Control::FormatResponse;
using Desert::Editor::Control::IsShot;
using Desert::Editor::Control::kOps;
using Desert::Editor::Control::kStateSections;
using Desert::Editor::Control::Op;
using Desert::Editor::Control::ParseRequest;
using Desert::Editor::Control::PendingWork;
using Desert::Editor::Control::Request;
using Desert::Editor::Control::Response;
using Desert::Editor::Control::ToJson;
using Desert::Editor::Control::ValidateSections;

namespace
{
    Request ParseOk( const std::string& line )
    {
        const auto parsed = ParseRequest( line );
        EXPECT_TRUE( parsed.IsSuccess() ) << line << " -> " << ( parsed.IsSuccess() ? "" : parsed.GetError() );
        return parsed.IsSuccess() ? parsed.GetValue() : Request{};
    }

    std::string ParseError( const std::string& line )
    {
        const auto parsed = ParseRequest( line );
        EXPECT_FALSE( parsed.IsSuccess() ) << line << " was accepted and should not have been";
        return parsed.IsSuccess() ? std::string() : parsed.GetError();
    }

    // The reply, read back as JSON. The whole point of the format tests is that a CLIENT can read what
    // this writes, so they assert against a parse rather than against a substring where they can.
    rfl::Generic::Object ReadBack( const Response& response )
    {
        const auto parsed = rfl::json::read<rfl::Generic>( FormatResponse( response ) );
        EXPECT_TRUE( parsed ) << "a response that is not readable JSON is not a response";
        if ( !parsed )
            return {};
        const auto object = parsed.value().to_object();
        EXPECT_TRUE( object );
        return object ? object.value() : rfl::Generic::Object{};
    }

    std::string StringField( const rfl::Generic::Object& object, const char* key )
    {
        const auto field = object.get( key );
        if ( !field )
            return {};
        const auto text = field.value().to_string();
        return text ? text.value() : std::string{};
    }

    bool BoolField( const rfl::Generic::Object& object, const char* key, bool fallback )
    {
        const auto field = object.get( key );
        if ( !field )
            return fallback;
        const auto value = field.value().to_bool();
        return value ? value.value() : fallback;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// 1. Totality.
// ---------------------------------------------------------------------------------------------------

// EVERY operation in the table parses. Walked rather than listed, so an operation added to kOps without a
// case in ParseRequest fails here instead of at the first client that tries it.
TEST( ControlProtocol, EveryKnownOperationParses )
{
    for ( const auto& spec : kOps )
    {
        // Each op is given the fields it requires; the point is that the NAME is accepted and maps to the
        // enumerator it is paired with in the table.
        std::string line = std::string( R"({"id":1,"op":")" ) + spec.Name + R"(")";
        if ( spec.Operation == Op::Run )
            line += R"(,"group":"Panel","label":"Open Details")";
        if ( IsShot( spec.Operation ) )
            line += R"(,"path":"/tmp/shot.png")";
        line += "}";

        const Request request = ParseOk( line );
        EXPECT_EQ( static_cast<int>( request.Operation ), static_cast<int>( spec.Operation ) )
             << "'" << spec.Name << "' parsed as a different operation from the one the table pairs it with";
    }
}

TEST( ControlProtocol, AnUnknownOperationIsNamedAndListsTheKnownOnes )
{
    const std::string message = ParseError( R"({"id":1,"op":"open-panel"})" );

    EXPECT_NE( message.find( "open-panel" ), std::string::npos ) << "the refusal must quote what was sent";
    for ( const auto& spec : kOps )
    {
        EXPECT_NE( message.find( spec.Name ), std::string::npos )
             << "'" << spec.Name << "' is accepted by the parser and missing from the list it prints";
    }
}

TEST( ControlProtocol, TextThatIsNotAJsonObjectIsRefusedRatherThanIgnored )
{
    EXPECT_FALSE( ParseError( "" ).empty() );
    EXPECT_FALSE( ParseError( "not json at all" ).empty() );
    EXPECT_FALSE( ParseError( R"(["run"])" ).empty() ) << "an array is JSON and is not a request";
    EXPECT_FALSE( ParseError( R"({"id":1})" ).empty() ) << "no operation named";
    EXPECT_FALSE( ParseError( R"({"id":1,"op":""})" ).empty() );
}

// ---------------------------------------------------------------------------------------------------
// 2. Incomplete requests are refused.
// ---------------------------------------------------------------------------------------------------

TEST( ControlProtocol, RunNeedsBothHalvesOfTheAddress )
{
    EXPECT_FALSE( ParseError( R"({"id":1,"op":"run","group":"Panel"})" ).empty() );
    EXPECT_FALSE( ParseError( R"({"id":1,"op":"run","label":"Open Details"})" ).empty() );
    EXPECT_FALSE( ParseError( R"({"id":1,"op":"run","group":"","label":"Open Details"})" ).empty() );

    const Request request = ParseOk( R"({"id":7,"op":"run","group":"Panel","label":"Open Details"})" );
    EXPECT_EQ( request.Id, 7 );
    EXPECT_EQ( request.Group, "Panel" );
    EXPECT_EQ( request.Label, "Open Details" );
}

// A capture with nowhere to go would report success and leave no evidence -- which is the failure the whole
// capture family exists to prevent, arriving through the door marked "convenience".
TEST( ControlProtocol, AShotNeedsSomewhereToWrite )
{
    EXPECT_FALSE( ParseError( R"({"id":1,"op":"shot.window"})" ).empty() );
    EXPECT_FALSE( ParseError( R"({"id":1,"op":"shot.viewport","path":""})" ).empty() );

    EXPECT_EQ( ParseOk( R"({"id":1,"op":"shot.window","path":"/tmp/a.png"})" ).Path, "/tmp/a.png" );
}

// ---------------------------------------------------------------------------------------------------
// 3. An outcome is always present.
// ---------------------------------------------------------------------------------------------------

TEST( ControlProtocol, EveryResponseCarriesItsOutcome )
{
    const rfl::Generic::Object success = ReadBack( Response::Success( 3 ) );
    EXPECT_TRUE( BoolField( success, "ok", false ) );
    EXPECT_TRUE( success.get( "id" ) ) << "a client that pipelines cannot match a reply without one";

    const rfl::Generic::Object failure = ReadBack( Response::Failure( 4, "the scene has no camera" ) );
    EXPECT_FALSE( BoolField( failure, "ok", true ) );
    EXPECT_EQ( StringField( failure, "error" ), "the scene has no camera" );
}

// A refusal with nothing said is the exact shape this project keeps removing. It cannot be built here --
// and rather than accepting an empty reason quietly, the factory produces a message that names the CHANNEL
// as the fault, because that is what it is. Loud and wrong beats silent and wrong: somebody reads it.
TEST( ControlProtocol, ARefusalWithNoReasonBlamesTheChannelRatherThanSayingNothing )
{
    const rfl::Generic::Object failure = ReadBack( Response::Failure( 5, "" ) );

    EXPECT_FALSE( BoolField( failure, "ok", true ) );
    const std::string reason = StringField( failure, "error" );
    EXPECT_FALSE( reason.empty() ) << "a response that failed and said nothing is the whole defect";
    EXPECT_NE( reason.find( "defect in the control channel" ), std::string::npos );
}

// A payload cannot displace the outcome. The fields a client depends on are written after the payload is
// copied in, so a command whose result happened to carry a key called "ok" cannot make a failure read as a
// success.
TEST( ControlProtocol, APayloadCannotOverwriteTheOutcome )
{
    rfl::Generic::Object payload;
    payload["ok"]    = rfl::Generic( false );
    payload["id"]    = rfl::Generic( 999.0 );
    payload["error"] = rfl::Generic( std::string( "not really" ) );

    const rfl::Generic::Object written = ReadBack( Response::Success( 11, payload ) );
    EXPECT_TRUE( BoolField( written, "ok", false ) );
    EXPECT_FALSE( written.get( "error" ) ) << "a success must not carry an error field";
}

// The framing is one message per line, so a payload carrying a newline would split one reply into two --
// and the second half would be read as the answer to the NEXT request.
TEST( ControlProtocol, AResponseIsOneLine )
{
    rfl::Generic::Object payload;
    payload["message"] = rfl::Generic( std::string( "first\nsecond\r\nthird" ) );

    const std::string written = FormatResponse( Response::Success( 1, payload ) );
    EXPECT_EQ( written.find( '\n' ), std::string::npos );
    EXPECT_EQ( written.find( '\r' ), std::string::npos );
}

// ---------------------------------------------------------------------------------------------------
// 4. Round trip.
// ---------------------------------------------------------------------------------------------------

// A refusal produced by the editor has to survive the wire and arrive as the same words. A message that is
// mangled in transit is a diagnosis nobody can act on, and quotes badly in a report.
TEST( ControlProtocol, ARefusalSurvivesBeingWrittenAndReadBack )
{
    const std::string reason =
         R"(no command 'Panel' / 'Open "Details"' is offered right now (3 available). Did you mean...?)";

    EXPECT_EQ( StringField( ReadBack( Response::Failure( 2, reason ) ), "error" ), reason );
}

// ---------------------------------------------------------------------------------------------------
// 5. The two captures are two operations.
// ---------------------------------------------------------------------------------------------------

// THE DISTINCTION THIS CHANNEL WAS BUILT ON. `shot.window` reads the presented swapchain image and contains
// the panels, the menus and the dialogs; `shot.viewport` reads the scene's own final image and contains
// none of them, because ImGui is recorded into the swapchain pass. Before this channel, only the second
// existed -- which is why no capture this engine ever took held one pixel of its interface, and why proving
// anything about a panel meant photographing the window from outside the process.
//
// They are separate operations rather than one with a flag precisely so that neither can quietly stand in
// for the other.
TEST( ControlProtocol, TheWindowShotAndTheViewportShotAreDistinctOperations )
{
    const Request window   = ParseOk( R"({"id":1,"op":"shot.window","path":"/tmp/w.png"})" );
    const Request viewport = ParseOk( R"({"id":2,"op":"shot.viewport","path":"/tmp/v.png"})" );

    EXPECT_EQ( window.Operation, Op::ShotWindow );
    EXPECT_EQ( viewport.Operation, Op::ShotViewport );
    EXPECT_NE( static_cast<int>( window.Operation ), static_cast<int>( viewport.Operation ) );

    EXPECT_TRUE( IsShot( window.Operation ) );
    EXPECT_TRUE( IsShot( viewport.Operation ) );
    EXPECT_FALSE( IsShot( Op::Run ) ) << "IsShot decides what waits for a settled frame AND gets captured";
    EXPECT_FALSE( IsShot( Op::State ) );
    EXPECT_FALSE( IsShot( Op::Commands ) );
    EXPECT_FALSE( IsShot( Op::Quit ) );
}

// ---------------------------------------------------------------------------------------------------
// 6. State sections.
// ---------------------------------------------------------------------------------------------------

TEST( ControlProtocol, EveryKnownSectionIsAccepted )
{
    for ( const char* section : kStateSections )
        EXPECT_TRUE( ValidateSections( { section } ).IsSuccess() ) << section;

    EXPECT_TRUE( ValidateSections( {} ).IsSuccess() ) << "empty means all of them";
}

TEST( ControlProtocol, AnUnknownSectionIsRefusedAndTheKnownOnesListed )
{
    const auto refused = ValidateSections( { "documents", "everything" } );
    ASSERT_FALSE( refused.IsSuccess() );

    const std::string message = refused.GetError();
    EXPECT_NE( message.find( "everything" ), std::string::npos );
    for ( const char* section : kStateSections )
        EXPECT_NE( message.find( section ), std::string::npos ) << section << " is served and not listed";
}

// EVERY section, asked for on its own, comes back. Walked rather than spot-checked: a section in the table
// with no branch in ToJson would validate, return nothing, and read as "the editor has none of those".
TEST( ControlProtocol, EverySectionInTheTableIsActuallySerialised )
{
    EditorSnapshot snapshot;
    snapshot.SceneName = "Clouds_Protocol";

    for ( const char* section : kStateSections )
    {
        const rfl::Generic::Object json = ToJson( snapshot, { section } );
        EXPECT_TRUE( json.get( section ) )
             << "'" << section << "' is offered by ValidateSections and produced no output";
    }
}

TEST( ControlProtocol, AskingForOneSectionDoesNotReturnTheOthers )
{
    EditorSnapshot snapshot;
    snapshot.SceneName = "Clouds_Protocol";

    const rfl::Generic::Object json = ToJson( snapshot, { "scene" } );
    EXPECT_TRUE( json.get( "scene" ) );
    EXPECT_FALSE( json.get( "documents" ) );
    EXPECT_FALSE( json.get( "panels" ) );
}

// THE DOCUMENTS SECTION IS WHAT THE ACCEPTANCE OF THIS TASK RESTS ON: the open documents in most-recently-
// used order, and the list of the ones that were CLOSED -- state that outlives the window it describes and
// which nothing could photograph before, because closing a window needs a mouse.
TEST( ControlProtocol, TheDocumentsSectionCarriesBothTheOpenOnesAndTheClosedOnes )
{
    EditorSnapshot snapshot;
    snapshot.Documents.push_back( { .Name               = "M_Crate",
                                    .Type               = "SurfaceMaterial",
                                    .Subject            = "1111",
                                    .HoldsRendererSlot  = true,
                                    .ClaimsRendererSlot = true,
                                    .Focused            = true } );
    snapshot.RecentlyClosed.push_back( { .Name = "M_Barrel", .Type = "SurfaceMaterial", .Subject = "2222" } );

    const rfl::Generic::Object json      = ToJson( snapshot, { "documents" } );
    const auto                 documents = json.get( "documents" );
    ASSERT_TRUE( documents );

    const auto object = documents.value().to_object();
    ASSERT_TRUE( object );

    const auto open = object.value().get( "open" );
    ASSERT_TRUE( open );
    const auto openArray = open.value().to_array();
    ASSERT_TRUE( openArray );
    ASSERT_EQ( openArray.value().size(), 1u );

    const auto first = openArray.value()[0].to_object();
    ASSERT_TRUE( first );
    EXPECT_EQ( StringField( first.value(), "name" ), "M_Crate" );
    EXPECT_TRUE( BoolField( first.value(), "holdsSlot", false ) );
    EXPECT_TRUE( BoolField( first.value(), "focused", false ) );

    const auto closed = object.value().get( "recentlyClosed" );
    ASSERT_TRUE( closed );
    const auto closedArray = closed.value().to_array();
    ASSERT_TRUE( closedArray );
    ASSERT_EQ( closedArray.value().size(), 1u );

    const auto onlyClosed = closedArray.value()[0].to_object();
    ASSERT_TRUE( onlyClosed );
    EXPECT_EQ( StringField( onlyClosed.value(), "name" ), "M_Barrel" );
}

// The quiescence section speaks the SAME vocabulary a settle timeout does, so a client that read
// "asset documents are waiting to be opened" here recognises it when a refusal quotes it back.
TEST( ControlProtocol, TheQuiescenceSectionNamesOutstandingWorkInTheSameWordsARefusalDoes )
{
    EditorSnapshot snapshot;
    snapshot.Quiescence.Set( PendingWork::AssetOpens, true );

    const auto section = ToJson( snapshot, { "quiescence" } ).get( "quiescence" );
    ASSERT_TRUE( section );
    const auto object = section.value().to_object();
    ASSERT_TRUE( object );

    EXPECT_FALSE( BoolField( object.value(), "settled", true ) );
    EXPECT_EQ( StringField( object.value(), "outstanding" ), EditorQuiescence( snapshot.Quiescence ).Describe() );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
