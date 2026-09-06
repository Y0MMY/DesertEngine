// The launcher's own contracts, all of them written as a RELATION between two sides rather than as
// a snapshot of one:
//
//   * what the launcher hands the OS is what the other process receives — asserted by actually
//     starting a process and reading back the argv it got;
//   * what the launcher writes into a project is what the launcher (and therefore the engine,
//     through the same desert-shared reader) reads back out;
//   * what the launcher scaffolds on disk is the shared content-folder census, not a second list;
//   * what the launcher shows on a card is the descriptor's own name, not the file stem.
//
// <DesertShared/LaunchProtocol.hpp> already declared that "the launcher asserts the command it
// composes uses exactly these constants". That test did not exist. This is it.

#include <gtest/gtest.h>

#include <DesertShared/LaunchProtocol.hpp>
#include <DesertShared/ProjectFormat.hpp>

#include "Files.hpp"
#include "Launch.hpp"
#include "Projects.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // Every shell metacharacter that mattered, in one path. Measured against the OLD shell-string
    // composition on macOS: the space survived, `$HOME` was substituted, the quote broke the
    // command outright (the Editor never started, and the hub closed anyway), and `id -u` inside
    // backticks was EXECUTED with its output spliced into the path.
    const std::string kHostileDeproj = "/tmp/Odd $HOME \"q\" & `id -u`/My Game.deproj";

    // An engine root that `std::filesystem` calls ABSOLUTE ON THIS HOST. `/opt/desert` is absolute on
    // POSIX and NOT on Windows — a leading slash there carries a root directory but no root NAME, so
    // `is_absolute()` is false and every assertion built on it fails on Windows alone. Written once,
    // here, so a future fixture cannot reintroduce the same platform-blind literal.
#ifdef _WIN32
    const std::string kAbsoluteEngineRoot = "C:\\opt\\desert";
#else
    const std::string kAbsoluteEngineRoot = "/opt/desert";
#endif

    fs::path MakeTempDirectory( const std::string& label )
    {
        const fs::path directory =
             fs::temp_directory_path() /
             ( "desert-hub-" + label + "-" +
               std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
        std::error_code ec;
        fs::remove_all( directory, ec );
        fs::create_directories( directory, ec );
        return directory;
    }

    std::vector<std::string> ReadLines( const fs::path& file )
    {
        std::vector<std::string> lines;
        std::ifstream            in( file );
        std::string              line;
        while ( std::getline( in, line ) )
            lines.push_back( line );
        return lines;
    }
} // namespace

// ── D-launch: the command the launcher composes ──────────────────────────────────────────────────

TEST( ProjectHubLaunch, TheProjectPathTravelsAsBytesAndNotAsAString )
{
    const Hub::LaunchCommand command =
         Hub::BuildEditorLaunch( "/opt/desert", Common::Launch::kConfigRelease, kHostileDeproj );

    // The flag comes from the shared protocol header, appears once, and the path is the very next
    // element — that adjacency is the whole protocol, and it is why nothing needs escaping.
    const auto flag = std::find( command.Arguments.begin(), command.Arguments.end(),
                                 std::string( Common::Launch::kProjectFlag ) );
    ASSERT_NE( flag, command.Arguments.end() ) << "the launch does not carry " << Common::Launch::kProjectFlag;
    EXPECT_EQ( std::count( command.Arguments.begin(), command.Arguments.end(),
                           std::string( Common::Launch::kProjectFlag ) ),
               1 );
    ASSERT_NE( flag + 1, command.Arguments.end() ) << Common::Launch::kProjectFlag << " has no value";
    EXPECT_EQ( *( flag + 1 ), kHostileDeproj )
         << "the path was rewritten on the way out — an argv element must be handed over verbatim, "
            "with no quoting, because nothing downstream re-parses it";
}

TEST( ProjectHubLaunch, TheConfigurationNameIsTheProtocolSpelling )
{
    // Both configurations, and both platforms' shapes: macOS passes the name to RunEditor.sh as an
    // argument, Windows puts it in build\Bin\<config>\Editor.exe. Either way the spelling that ends
    // up in the command is the protocol's, not a second literal in the launcher.
    for ( const char* config : { Common::Launch::kConfigDebug, Common::Launch::kConfigRelease } )
    {
        // THE ENGINE ROOT MUST BE ABSOLUTE **BY THE HOST'S RULES**, and `/opt/desert` is not, on
        // Windows: a leading slash with no drive letter has no root NAME there, so `is_absolute()`
        // is false and the assertion below failed on Windows Release only. The product was never
        // wrong — the fixture was, and it was invisible on macOS by construction. Same family as
        // `far` being a windef.h macro: a platform whose rules nobody here can run locally.
        const Hub::LaunchCommand command = Hub::BuildEditorLaunch( kAbsoluteEngineRoot, config, "/p/P.deproj" );

        std::string whole = command.Program;
        for ( const std::string& argument : command.Arguments )
            whole += " " + argument;
        EXPECT_NE( whole.find( config ), std::string::npos )
             << config << " does not appear in the launch: " << whole;
        EXPECT_TRUE( fs::path( command.Program ).is_absolute() )
             << "the program must be addressed by absolute path, never found through PATH: " << command.Program;
    }
}

TEST( ProjectHubLaunch, TheFileManagerIsAnOsToolAddressedByAbsolutePath )
{
    const Hub::LaunchCommand command = Hub::BuildRevealCommand( "/tmp/My Game/My Game.deproj" );
    EXPECT_TRUE( fs::path( command.Program ).is_absolute() ) << command.Program;
    EXPECT_FALSE( command.Arguments.empty() );
}

TEST( ProjectHubLaunch, AProgramThatIsNotThereIsARefusalWithItsName )
{
    // The old LaunchEditor discarded std::system()'s result and returned true unconditionally, so a
    // launch that never happened still closed the window.
    Hub::LaunchCommand command;
    command.Program    = "/nonexistent/desert/Editor";
    const auto spawned = Hub::SpawnDetached( command );
    ASSERT_FALSE( spawned.IsSuccess() );
    EXPECT_NE( spawned.GetError().find( command.Program ), std::string::npos ) << spawned.GetError();
}

#ifndef _WIN32
TEST( ProjectHubLaunch, TheProcessOnTheOtherSideReceivesExactlyThoseBytes )
{
    // The end-to-end half: compose the launch for a hostile path, actually spawn it, and read back
    // the argv the started process saw. POSIX-only because it needs a recorder the kernel can exec
    // from a shebang; the composition tests above cover Windows, where CreateProcessW's command
    // line is built by the documented inverse of CommandLineToArgvW.
    const fs::path root     = MakeTempDirectory( "argv" );
    const fs::path recorder = root / "scripts" / "MacOS" / "RunEditor.sh";
    const fs::path recorded = root / "argv.txt";
    fs::create_directories( recorder.parent_path() );

    {
        std::ofstream out( recorder );
        out << "#!/bin/sh\n"
            << ": > '" << recorded.string() << "'\n"
            << "for a in \"$@\"; do printf '%s\\n' \"$a\" >> '" << recorded.string() << "'; done\n";
    }
    fs::permissions( recorder, fs::perms::owner_all );

    const auto spawned = Hub::SpawnDetached(
         Hub::BuildEditorLaunch( root.string(), Common::Launch::kConfigRelease, kHostileDeproj ) );
    ASSERT_TRUE( spawned.IsSuccess() ) << spawned.GetError();

    // The child is not waited for (the hub never waits either), so poll for its output.
    for ( int attempt = 0; attempt < 200 && !fs::exists( recorded ); ++attempt )
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
    ASSERT_TRUE( fs::exists( recorded ) ) << "the recorder never ran";
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // let it finish writing

    const std::vector<std::string> expected = { Common::Launch::kConfigRelease, Common::Launch::kProjectFlag,
                                                kHostileDeproj };
    EXPECT_EQ( ReadLines( recorded ), expected )
         << "the argv that crossed the process boundary is not the argv that was composed";

    std::error_code ec;
    fs::remove_all( root, ec );
}
#endif

// ── the recent-projects registry ─────────────────────────────────────────────────────────────────

TEST( ProjectHubRecent, TheListIsNotTruncated )
{
    // The cap of ten was silent: the eleventh project simply stopped existing, with no message. It
    // is gone here AND in the engine's ProjectContext::RegisterRecent, which writes the same file —
    // lifting only one of the two would have made the engine erase what the launcher kept.
    std::vector<std::string> recent;
    for ( int i = 0; i < 25; ++i )
        Hub::PromoteRecent( recent, "/p/P" + std::to_string( i ) + ".deproj" );
    ASSERT_EQ( recent.size(), 25u ) << "an entry was dropped without anyone being told";
    EXPECT_EQ( recent.front(), "/p/P24.deproj" ) << "most recent first";
    EXPECT_EQ( recent.back(), "/p/P0.deproj" );
}

TEST( ProjectHubRecent, ReopeningAProjectMovesItAndDoesNotDuplicateIt )
{
    std::vector<std::string> recent = { "/p/A.deproj", "/p/B.deproj", "/p/C.deproj" };
    Hub::PromoteRecent( recent, "/p/C.deproj" );
    EXPECT_EQ( recent, ( std::vector<std::string>{ "/p/C.deproj", "/p/A.deproj", "/p/B.deproj" } ) );
}

TEST( ProjectHubRecent, TheNameOnTheCardIsTheNameTheEngineWillRead )
{
    // The stem and the descriptor's Name are two different facts, and a card used to show the
    // first while claiming to be the second. They only coincide for a project the hub created
    // itself, which is exactly why a test built on CreateProject alone cannot see this: the
    // descriptor here is named ONE thing and filed under ANOTHER, as a project renamed in the
    // Editor, packaged, or moved by hand ends up being.
    const fs::path root       = MakeTempDirectory( "naming" );
    const fs::path deprojPath = root / "folder-name.deproj";

    Common::Project::ProjectFile descriptor;
    descriptor.Name = "Dune Racer";
    ASSERT_TRUE( Hub::WriteTextFile( deprojPath, Common::Project::WriteProjectFile( descriptor ) ).IsSuccess() );

    const Hub::ProjectEntry entry = Hub::ResolveProjectEntry( deprojPath.string() );
    ASSERT_TRUE( entry.IsOpenable() ) << entry.Trouble;
    EXPECT_EQ( entry.Name, "Dune Racer" ) << "the card is showing the file stem, not the project";

    std::error_code ec;
    fs::remove_all( root, ec );
}

TEST( ProjectHubRecent, AnEntryThatCannotBeOpenedSaysSoBeforeItIsClicked )
{
    const fs::path root = MakeTempDirectory( "entries" );

    // 1. Gone from disk. This is the live case: a registry full of paths into worktrees that have
    //    since been reclaimed used to draw as working projects with a live Open button.
    const Hub::ProjectEntry missing = Hub::ResolveProjectEntry( ( root / "Gone" / "Gone.deproj" ).string() );
    EXPECT_FALSE( missing.IsOpenable() );
    EXPECT_FALSE( missing.Trouble.empty() );

    // 2. Present but corrupt — the engine refuses it into a log the user never sees, so the card
    //    has to refuse it too, and with the PARSER's own words.
    const fs::path corrupt = root / "Corrupt.deproj";
    ASSERT_TRUE( Hub::WriteTextFile( corrupt, "{ this is not a descriptor" ).IsSuccess() );
    const Hub::ProjectEntry broken = Hub::ResolveProjectEntry( corrupt.string() );
    EXPECT_FALSE( broken.IsOpenable() );
    EXPECT_FALSE( broken.Trouble.empty() );

    std::error_code ec;
    fs::remove_all( root, ec );
}

// ── project names ────────────────────────────────────────────────────────────────────────────────

TEST( ProjectHubNames, ANameIsAFolderNameAndNotAPath )
{
    // Shell metacharacters are FINE — they are ordinary bytes in a folder name on both platforms,
    // and the argv spawn above is why they no longer have to be feared.
    EXPECT_EQ( Hub::ValidateProjectName( "My Game" ), "" ) << "a space is not a problem";
    EXPECT_EQ( Hub::ValidateProjectName( "Neo & $Dune 2" ), "" ) << "nor are & and $";

    // Each of these used to scaffold something, somewhere. Note the split from the FORMAT's rules:
    // desert-shared's suite round-trips a quote inside ProjectFile::Name, and must — a descriptor
    // can be authored by hand. The launcher refuses one here because the same string also becomes a
    // DIRECTORY, and a project created on macOS is meant to open on Windows.
    for ( const char* bad :
          { "", "   ", "..", ".", "a/b", "a\\b", "why?", "Neo \"Dune\" 2", "trailing.", "trailing " } )
        EXPECT_NE( Hub::ValidateProjectName( bad ), "" ) << "accepted '" << bad << "'";
}

// ── creation: what is scaffolded is what the engine expects ──────────────────────────────────────

TEST( ProjectHubCreate, TheProjectItWritesIsTheProjectItReadsBack )
{
    const fs::path location = MakeTempDirectory( "create" );
    // A name that is a legal folder on both platforms and a minefield for a shell — which is
    // exactly the combination the launcher has to survive end to end: it becomes a directory, a
    // JSON string, and an argv element.
    const std::string           name  = "Neo & $Dune 2";
    const Hub::ProjectTemplate& blank = Hub::Templates().front();

    auto created = Hub::CreateProject( location.string(), name, blank );
    ASSERT_TRUE( created.IsSuccess() ) << created.GetError();
    const std::string deprojPath = created.ExtractValue();

    // Round trip through the SAME resolution a card uses: the displayed name is the descriptor's.
    const Hub::ProjectEntry entry = Hub::ResolveProjectEntry( deprojPath );
    EXPECT_TRUE( entry.IsOpenable() ) << entry.Trouble;
    EXPECT_EQ( entry.Name, name ) << "the card would show the file stem instead of the name the engine will read";

    // And the folders are the SHARED census, not a second list that happens to agree today.
    const fs::path root   = fs::path( deprojPath ).parent_path();
    const auto     parsed = Common::Project::ReadProjectFile( Hub::ReadTextFile( deprojPath ).GetValue() );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    for ( const std::string_view folder : Common::Project::StandardContentFolders )
        EXPECT_TRUE( fs::exists( root / parsed.GetValue().AssetsRoot / folder ) )
             << "census row '" << folder << "' was not scaffolded";

    std::error_code ec;
    fs::remove_all( location, ec );
}

TEST( ProjectHubCreate, AFailedCreateNamesItsStepAndLeavesNothingBehind )
{
    // The failure has to strike AFTER some of the tree exists, or the cleanup is not being tested
    // at all. A folder name past the filesystem's component limit does that on both platforms: the
    // seven census rows are created, then this one is refused.
    //
    // Two defects meet here. The census loop used to share ONE error_code, which std::filesystem
    // clears on every success — a failure in the middle was erased by the next row that worked and
    // the project was reported created with a folder missing. And whatever did fail left its
    // half-built tree in the user's chosen location, so the next attempt with the same name was
    // refused with "folder already exists and is not empty".
    const fs::path    location = MakeTempDirectory( "failure" );
    const std::string tooLong( 400, 'x' );

    Hub::ProjectTemplate impossible = Hub::Templates().front();
    impossible.ExtraFolders         = { tooLong.c_str() };

    auto created = Hub::CreateProject( location.string(), "MyGame", impossible );
    ASSERT_FALSE( created.IsSuccess() ) << "a folder that cannot exist was reported as created";
    EXPECT_NE( created.GetError().find( "MyGame" ), std::string::npos )
         << "the refusal does not say where it failed: " << created.GetError();
    EXPECT_FALSE( fs::exists( location / "MyGame" ) ) << "a half-created project was left behind";

    std::error_code ec;
    fs::remove_all( location, ec );
}

TEST( ProjectHubCreate, AnOccupiedFolderIsRefusedBeforeAnythingIsWritten )
{
    const fs::path location = MakeTempDirectory( "occupied" );
    fs::create_directories( location / "Taken" );
    {
        std::ofstream out( location / "Taken" / "something.txt" );
        out << "x";
    }

    auto created = Hub::CreateProject( location.string(), "Taken", Hub::Templates().front() );
    EXPECT_FALSE( created.IsSuccess() );
    EXPECT_TRUE( fs::exists( location / "Taken" / "something.txt" ) ) << "the refusal deleted the user's files";

    std::error_code ec;
    fs::remove_all( location, ec );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
