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
#include "HubConfig.hpp"
#include "Launch.hpp"
#include "Projects.hpp"
#include "Thumbnails.hpp"

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

// The recency POLICY itself — no cap, most recent first, unique, LastOpened stamped — moved into
// desert-shared with the registry format, and its tests moved with it (RecentList.* in
// Tests/project_format_test.cpp). It lived here AND in the engine before that, over one shared
// file; two copies of a rule over one file is how both of them ended up carrying the same silent
// cap of ten. What stays on this side is what the LAUNCHER does with that registry.

TEST( ProjectHubRecent, WhatTheLauncherWritesTheEngineReadsBackOutOfTheSameFile )
{
    // End to end over the real file, in a temp config directory rather than the developer's own:
    // the launcher promotes a project, saves, and the SHARED reader — the one the engine's
    // ProjectContext::RecentProjects calls — gets back the same path, in the same place, with the
    // same time. This is the relation the two repositories cannot verify any other way.
    const fs::path config = MakeTempDirectory( "registry" );

    Common::Project::ProjectsRegistry registry;
    Common::Project::PromoteRecent( registry, "/p/Old.deproj", 1000 );
    Common::Project::PromoteRecent( registry, kHostileDeproj, 1757203200 );
    ASSERT_TRUE( Hub::SaveProjects( config.string(), registry ).IsSuccess() );

    // Read it the way the ENGINE would: raw bytes off disk, through the shared reader.
    const auto raw = Hub::ReadTextFile( Hub::ProjectsRegistryFile( config.string() ) );
    ASSERT_TRUE( raw.IsSuccess() ) << raw.GetError();
    auto reread = Common::Project::ReadProjectsRegistry( raw.GetValue() );
    ASSERT_TRUE( reread.IsSuccess() ) << reread.GetError();

    ASSERT_EQ( reread.GetValue().Projects.size(), 2u );
    EXPECT_EQ( reread.GetValue().Projects[0].Path, kHostileDeproj )
         << "a path full of shell metacharacters did not survive the file";
    EXPECT_EQ( reread.GetValue().Projects[0].LastOpened, 1757203200 )
         << "the time the tile draws did not reach the file";
    EXPECT_EQ( reread.GetValue().Projects[1].Path, "/p/Old.deproj" ) << "most recent first";

    std::error_code ec;
    fs::remove_all( config, ec );
}

TEST( ProjectHubRecent, ARegistryWrittenBeforeLastOpenedIsReadAndThenUpgradedInPlace )
{
    // The migration, over the actual file, from the launcher's side: a flat list is loaded whole,
    // and the next save leaves the CURRENT shape on disk. A developer's registry upgrades itself
    // the first time they open anything.
    const fs::path config = MakeTempDirectory( "migrate" );
    ASSERT_TRUE( Hub::WriteTextFile( Hub::ProjectsRegistryFile( config.string() ),
                                     R"({"Projects":["/p/A.deproj","/p/B.deproj"]})" )
                      .IsSuccess() );

    auto loaded = Hub::LoadProjects( config.string() );
    ASSERT_TRUE( loaded.IsSuccess() ) << loaded.GetError();
    Common::Project::ProjectsRegistry registry = loaded.ExtractValue();
    ASSERT_EQ( registry.Projects.size(), 2u ) << "the launcher lost projects on the way in";
    EXPECT_EQ( registry.Projects[0].Path, "/p/A.deproj" ) << "the order the old format carried was scrambled";

    Common::Project::PromoteRecent( registry, "/p/B.deproj", 99 );
    ASSERT_TRUE( Hub::SaveProjects( config.string(), registry ).IsSuccess() );

    const auto raw = Hub::ReadTextFile( Hub::ProjectsRegistryFile( config.string() ) );
    ASSERT_TRUE( raw.IsSuccess() );
    EXPECT_NE( raw.GetValue().find( "LastOpened" ), std::string::npos )
         << "the file on disk is still the old shape after a write: " << raw.GetValue();

    std::error_code ec;
    fs::remove_all( config, ec );
}

TEST( ProjectHubRecent, AMissingRegistryIsAnEmptyListAndAnUnreadableOneIsARefusal )
{
    // "the registry is empty" and "the registry could not be read" used to look identical to every
    // caller, and one of them means the user's project list is still on disk and about to be
    // overwritten with nothing.
    const fs::path config = MakeTempDirectory( "absent" );
    auto           fresh  = Hub::LoadProjects( config.string() );
    ASSERT_TRUE( fresh.IsSuccess() ) << "a fresh machine was reported as an error";
    EXPECT_TRUE( fresh.GetValue().Projects.empty() );

    ASSERT_TRUE( Hub::WriteTextFile( Hub::ProjectsRegistryFile( config.string() ), "{ not json" ).IsSuccess() );
    auto broken = Hub::LoadProjects( config.string() );
    ASSERT_FALSE( broken.IsSuccess() ) << "a corrupt registry was reported as an empty one";
    EXPECT_NE( broken.GetError().find( "projects.json" ), std::string::npos ) << broken.GetError();

    std::error_code ec;
    fs::remove_all( config, ec );
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

// ── templates as DATA, not as a property of this binary ──────────────────────────────────────────

namespace
{
    // Writes a template folder the way an author would: a manifest, optionally a payload.
    //
    // BINARY, AND THAT IS THE WHOLE POINT OF THE FIXTURE. Windows' text mode expands every '\n' into
    // "\r\n" on the way to disk, so a payload written here in text mode is 20 bytes where the literal
    // beside the assertion is 19. The launcher then copies those 20 bytes perfectly — Files.cpp reads
    // AND writes binary — and `ThePayloadIsCopiedByteForByteWithNoSubstitutions` compared the faithful
    // copy against a string the fixture had never actually written. It failed on Windows CI while the
    // code under test was correct, which is the worst kind of red: it accuses the subject of the
    // fixture's fault, and only a 40-minute job on a machine nobody develops on can report it.
    //
    // A test whose claim is "byte for byte" must lay down its own bytes byte for byte. Nothing here
    // wants line-ending translation — these are payload files whose exact contents are the assertion.
    void MakeTemplate( const fs::path& engineRoot, const std::string& id, const std::string& manifest,
                       const std::vector<std::pair<std::string, std::string>>& payload = {} )
    {
        const fs::path folder = engineRoot / "Templates" / id;
        fs::create_directories( folder );
        std::ofstream( folder / "template.json", std::ios::binary ) << manifest;
        for ( const auto& [relative, content] : payload )
        {
            const fs::path file = folder / "Payload" / relative;
            fs::create_directories( file.parent_path() );
            std::ofstream( file, std::ios::binary ) << content;
        }
    }
} // namespace

TEST( ProjectHubTemplates, ATemplateIsAFolderAndAddingOneIsDroppingItIn )
{
    // The point of the whole change: the set of templates is a property of the ENGINE INSTALL, not
    // of this binary. Two folders appear on disk and two cards exist — with no list anywhere in the
    // launcher naming either of them.
    const fs::path engine = MakeTempDirectory( "templates" );
    MakeTemplate( engine, "Blank", R"({"DisplayName":"Blank","SortKey":0})" );
    MakeTemplate( engine, "FirstPerson", R"({"DisplayName":"First Person","SortKey":10,
                                             "DefaultScene":"Assets/Scenes/Main.desce"})",
                  { { "Assets/Scenes/Main.desce", "{}" } } );

    const Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    EXPECT_TRUE( scan.Refusals.empty() ) << scan.Refusals.front();
    ASSERT_EQ( scan.Templates.size(), 2u );

    // SortKey, not alphabet and not directory order: "Blank" has to come first, and "Blank" sorts
    // after "First Person" alphabetically.
    EXPECT_EQ( scan.Templates[0].Manifest.DisplayName, "Blank" );
    EXPECT_EQ( scan.Templates[1].Manifest.DisplayName, "First Person" );
    EXPECT_EQ( scan.Templates[0].Id, "Blank" ) << "the Id is the folder name";

    std::error_code ec;
    fs::remove_all( engine, ec );
}

TEST( ProjectHubTemplates, CategoryOrdersTheScanBecauseTheOrderIsTheGrouping )
{
    // Category is a manifest field, so it needs a consumer or it is a dead setting — authors would
    // be asked to fill in something the launcher throws away. Its consumer is the New Project
    // screen's grouping, and the grouping is nothing but this ORDER: a heading is drawn wherever
    // the category changes, so the scan has to hand the screen its templates already grouped.
    const fs::path engine = MakeTempDirectory( "categories" );
    MakeTemplate( engine, "Blank", R"({"DisplayName":"Blank","SortKey":0})" );
    MakeTemplate( engine, "Racing", R"({"DisplayName":"Racing","Category":"Gameplay","SortKey":20})" );
    MakeTemplate( engine, "FirstPerson", R"({"DisplayName":"First Person","Category":"Gameplay","SortKey":10})" );
    MakeTemplate( engine, "Empty2D", R"({"DisplayName":"2D","Category":"2D","SortKey":0})" );

    const Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    ASSERT_EQ( scan.Templates.size(), 4u );

    // Uncategorised first — "" sorts before any name, and the common case gets no heading over it.
    EXPECT_EQ( scan.Templates[0].Manifest.DisplayName, "Blank" );
    EXPECT_EQ( scan.Templates[0].Manifest.Category, "" );
    // Then whole categories, contiguous, so one pass can draw one heading each.
    EXPECT_EQ( scan.Templates[1].Manifest.Category, "2D" );
    EXPECT_EQ( scan.Templates[2].Manifest.Category, "Gameplay" );
    EXPECT_EQ( scan.Templates[3].Manifest.Category, "Gameplay" );
    // And SortKey still decides INSIDE a category: 10 before 20, not alphabet.
    EXPECT_EQ( scan.Templates[2].Manifest.DisplayName, "First Person" );
    EXPECT_EQ( scan.Templates[3].Manifest.DisplayName, "Racing" );

    std::error_code ec;
    fs::remove_all( engine, ec );
}

TEST( ProjectHubTemplates, ATemplateThatDoesNotParseIsAMessageAndNeverASilentSkip )
{
    // A silent skip means a template that exists on disk and nowhere on screen, and the person who
    // mistyped its manifest has no way to find that out (L2 §3.4). The refusal has to carry the
    // parser's own words and name the file.
    const fs::path engine = MakeTempDirectory( "badtemplate" );
    MakeTemplate( engine, "Blank", R"({"DisplayName":"Blank"})" );
    MakeTemplate( engine, "Racing", R"({"DisplayName":null})" );
    fs::create_directories( engine / "Templates" / "NotATemplate" ); // a folder with no manifest at all

    const Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    EXPECT_EQ( scan.Templates.size(), 1u ) << "a broken template was drawn as a card";
    ASSERT_EQ( scan.Refusals.size(), 2u ) << "a folder that did not load was skipped in silence";

    const std::string joined = scan.Refusals[0] + "\n" + scan.Refusals[1];
    EXPECT_NE( joined.find( "Racing" ), std::string::npos ) << joined;
    EXPECT_NE( joined.find( "DisplayName" ), std::string::npos )
         << "the refusal does not say WHAT is wrong: " << joined;
    EXPECT_NE( joined.find( "NotATemplate" ), std::string::npos ) << joined;

    std::error_code ec;
    fs::remove_all( engine, ec );
}

TEST( ProjectHubTemplates, AThumbnailIsAFileOnDiskAndNeverAnAssumption )
{
    const fs::path engine = MakeTempDirectory( "tplthumb" );
    MakeTemplate( engine, "WithArt", R"({"DisplayName":"With Art"})" );
    MakeTemplate( engine, "NoArt", R"({"DisplayName":"No Art"})" );
    fs::create_directories( engine / "Templates" / "WithArt" / "Media" );
    std::ofstream( engine / "Templates" / "WithArt" / "Media" / "Thumbnail.png" ) << "not really a png";

    const Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    ASSERT_EQ( scan.Templates.size(), 2u );
    for ( const Hub::TemplateEntry& entry : scan.Templates )
    {
        if ( entry.Id == "WithArt" )
            EXPECT_FALSE( entry.ThumbnailPath.empty() );
        else
            EXPECT_TRUE( entry.ThumbnailPath.empty() )
                 << "a template with no Media/Thumbnail.png is claiming one: " << entry.ThumbnailPath;
    }

    std::error_code ec;
    fs::remove_all( engine, ec );
}

// ── creation: what is scaffolded is what the engine expects ──────────────────────────────────────

namespace
{
    Hub::TemplateEntry BlankTemplate( const fs::path& engineRoot )
    {
        MakeTemplate( engineRoot, "Blank", R"({"DisplayName":"Blank"})" );
        Hub::TemplateScan scan = Hub::ScanTemplates( engineRoot.string() );
        EXPECT_EQ( scan.Templates.size(), 1u );
        return scan.Templates.front();
    }
} // namespace

TEST( ProjectHubCreate, TheProjectItWritesIsTheProjectItReadsBack )
{
    const fs::path location = MakeTempDirectory( "create" );
    const fs::path engine   = MakeTempDirectory( "create-engine" );
    // A name that is a legal folder on both platforms and a minefield for a shell — which is
    // exactly the combination the launcher has to survive end to end: it becomes a directory, a
    // JSON string, and an argv element.
    const std::string name = "Neo & $Dune 2";

    auto created = Hub::CreateProject( location.string(), name, BlankTemplate( engine ), "0.1.492+abc" );
    ASSERT_TRUE( created.IsSuccess() ) << created.GetError();
    const std::string deprojPath = created.ExtractValue();

    // Round trip through the SAME resolution a tile uses: the displayed name is the descriptor's.
    const Hub::ProjectEntry entry = Hub::ResolveProjectEntry( deprojPath );
    EXPECT_TRUE( entry.IsOpenable() ) << entry.Trouble;
    EXPECT_EQ( entry.Name, name ) << "the tile would show the file stem instead of the name the engine will read";

    const fs::path root   = fs::path( deprojPath ).parent_path();
    const auto     parsed = Common::Project::ReadProjectFile( Hub::ReadTextFile( deprojPath ).GetValue() );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    // The folders are the SHARED census, not a second list that happens to agree today.
    for ( const std::string_view folder : Common::Project::StandardContentFolders )
        EXPECT_TRUE( fs::exists( root / parsed.GetValue().AssetsRoot / folder ) )
             << "census row '" << folder << "' was not scaffolded";
    // And the descriptor carries the three fields that did not exist before this change.
    EXPECT_EQ( parsed.GetValue().FileVersion, Common::Project::kProjectFileVersion );
    EXPECT_EQ( parsed.GetValue().EngineVersion, "0.1.492+abc" )
         << "the project does not record which engine made it";

    std::error_code ec;
    fs::remove_all( location, ec );
    fs::remove_all( engine, ec );
}

TEST( ProjectHubCreate, ThePayloadIsCopiedByteForByteWithNoSubstitutions )
{
    // A .desce is JSON full of GUID references to materials, so a textual replacement inside one is
    // a way to break a reference, not a way to personalise a scene (L2 §2.2). The payload here
    // contains the literal name of a DIFFERENT project; a launcher that "helpfully" rewrote it
    // would corrupt every template that ships content.
    const fs::path    location = MakeTempDirectory( "payload" );
    const fs::path    engine   = MakeTempDirectory( "payload-engine" );
    const std::string body     = R"({"Scene":"TemplateProject","MaterialGuids":["b8f1-TemplateProject"]})";
    MakeTemplate(
         engine, "First", R"({"DisplayName":"First","DefaultScene":"Assets/Scenes/Main.desce"})",
         { { "Assets/Scenes/Main.desce", body }, { "Assets/Scripts/Player.lua", "-- TemplateProject\n" } } );
    Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    ASSERT_EQ( scan.Templates.size(), 1u );

    auto created = Hub::CreateProject( location.string(), "MyGame", scan.Templates.front(), "0.1" );
    ASSERT_TRUE( created.IsSuccess() ) << created.GetError();
    const fs::path root = fs::path( created.GetValue() ).parent_path();

    const auto scene = Hub::ReadTextFile( root / "Assets/Scenes/Main.desce" );
    ASSERT_TRUE( scene.IsSuccess() ) << scene.GetError();
    EXPECT_EQ( scene.GetValue(), body ) << "the payload was rewritten on the way in";
    EXPECT_EQ( Hub::ReadTextFile( root / "Assets/Scripts/Player.lua" ).GetValue(), "-- TemplateProject\n" );

    // And the descriptor points at the scene the manifest named.
    const auto parsed = Common::Project::ReadProjectFile( Hub::ReadTextFile( created.GetValue() ).GetValue() );
    ASSERT_TRUE( parsed.IsSuccess() );
    EXPECT_EQ( parsed.GetValue().DefaultScene, "Assets/Scenes/Main.desce" );

    std::error_code ec;
    fs::remove_all( location, ec );
    fs::remove_all( engine, ec );
}

TEST( ProjectHubCreate, ATemplateThatNamesASceneItDoesNotShipIsRefusedHereAndNotByTheEditor )
{
    // The launcher knows both the manifest and the tree it just laid down. "Create & Open" that
    // opens into a missing-scene error is a failure it had every fact needed to prevent — and the
    // half-made project must not be left behind either.
    const fs::path location = MakeTempDirectory( "noscene" );
    const fs::path engine   = MakeTempDirectory( "noscene-engine" );
    MakeTemplate( engine, "Broken", R"({"DisplayName":"Broken","DefaultScene":"Assets/Scenes/Main.desce"})" );
    Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    ASSERT_EQ( scan.Templates.size(), 1u );

    auto created = Hub::CreateProject( location.string(), "MyGame", scan.Templates.front(), "0.1" );
    ASSERT_FALSE( created.IsSuccess() )
         << "a project pointing at a scene that does not exist was reported created";
    EXPECT_NE( created.GetError().find( "Assets/Scenes/Main.desce" ), std::string::npos ) << created.GetError();
    EXPECT_FALSE( fs::exists( location / "MyGame" ) ) << "the half-made project was left behind";

    std::error_code ec;
    fs::remove_all( location, ec );
    fs::remove_all( engine, ec );
}

TEST( ProjectHubCreate, AFailedCreateNamesItsStepAndLeavesNothingBehind )
{
    // The failure has to strike AFTER some of the tree exists, or the cleanup is not being tested
    // at all. This template ships a FILE called `Assets`; the payload copy succeeds, and the census
    // pass that follows then cannot make `Assets/Meshes/` because `Assets` is not a directory.
    //
    // Two defects meet here. The census loop used to share ONE error_code, which std::filesystem
    // clears on every success — a failure in the middle was erased by the next row that worked and
    // the project was reported created with a folder missing. And whatever did fail left its
    // half-built tree in the user's chosen location, so the next attempt with the same name was
    // refused with "folder already exists and is not empty".
    const fs::path location = MakeTempDirectory( "failure" );
    const fs::path engine   = MakeTempDirectory( "failure-engine" );
    MakeTemplate( engine, "Hostile", R"({"DisplayName":"Hostile"})",
                  { { "Assets", "I am a file, not a folder" } } );
    Hub::TemplateScan scan = Hub::ScanTemplates( engine.string() );
    ASSERT_EQ( scan.Templates.size(), 1u );

    auto created = Hub::CreateProject( location.string(), "MyGame", scan.Templates.front(), "0.1" );
    ASSERT_FALSE( created.IsSuccess() ) << "a folder that cannot exist was reported as created";
    EXPECT_NE( created.GetError().find( "MyGame" ), std::string::npos )
         << "the refusal does not say where it failed: " << created.GetError();
    EXPECT_FALSE( fs::exists( location / "MyGame" ) ) << "a half-created project was left behind";

    std::error_code ec;
    fs::remove_all( location, ec );
    fs::remove_all( engine, ec );
}

TEST( ProjectHubCreate, AnOccupiedFolderIsRefusedBeforeAnythingIsWritten )
{
    const fs::path location = MakeTempDirectory( "occupied" );
    const fs::path engine   = MakeTempDirectory( "occupied-engine" );
    fs::create_directories( location / "Taken" );
    {
        std::ofstream out( location / "Taken" / "something.txt" );
        out << "x";
    }

    auto created = Hub::CreateProject( location.string(), "Taken", BlankTemplate( engine ), "0.1" );
    EXPECT_FALSE( created.IsSuccess() );
    EXPECT_TRUE( fs::exists( location / "Taken" / "something.txt" ) ) << "the refusal deleted the user's files";

    std::error_code ec;
    fs::remove_all( location, ec );
    fs::remove_all( engine, ec );
}

// ── the tile: what it says, and what it refuses to claim ─────────────────────────────────────────

TEST( ProjectHubTile, ATileWithNoThumbnailFileDoesNotPretendItHasOne )
{
    // The tile is a picture first, so "there is a file here" and "there could be a file here" are
    // two states it draws differently — and the second must never be mistaken for the first. The
    // answer comes from the DISK, never from the path.
    const fs::path root = MakeTempDirectory( "thumb" );

    Common::Project::ProjectFile descriptor;
    descriptor.Name = "Dunefall";
    ASSERT_TRUE( Hub::WriteTextFile( root / "Dunefall.deproj", Common::Project::WriteProjectFile( descriptor ) )
                      .IsSuccess() );

    Hub::ProjectEntry entry = Hub::ResolveProjectEntry( ( root / "Dunefall.deproj" ).string() );
    EXPECT_EQ( entry.ThumbnailPath, "" ) << "a project with no .thumbnail.png is claiming one";
    EXPECT_EQ( Hub::ProjectThumbnailPath( ( root / "Dunefall.deproj" ).string() ), "" );

    // Now the Editor writes one, beside the descriptor and under the name both sides agree on.
    ASSERT_TRUE( Hub::WriteTextFile( root / ".thumbnail.png", "png bytes" ).IsSuccess() );
    entry = Hub::ResolveProjectEntry( ( root / "Dunefall.deproj" ).string() );
    EXPECT_EQ( entry.ThumbnailPath, ( root / ".thumbnail.png" ).string() )
         << "the launcher does not look where the Editor writes";

    std::error_code ec;
    fs::remove_all( root, ec );
}

TEST( ProjectHubTile, ADescriptionReachesTheTileBecauseItIsWhyTheFieldExists )
{
    const fs::path root = MakeTempDirectory( "desc" );

    Common::Project::ProjectFile descriptor;
    descriptor.Name        = "Dunefall";
    descriptor.Description = "Third-person prototype. Sand dunes, one vehicle, no HUD yet.";
    ASSERT_TRUE(
         Hub::WriteTextFile( root / "D.deproj", Common::Project::WriteProjectFile( descriptor ) ).IsSuccess() );

    const Hub::ProjectEntry entry = Hub::ResolveProjectEntry( ( root / "D.deproj" ).string() );
    EXPECT_EQ( entry.Description, descriptor.Description )
         << "Description is a field with no consumer, which is a field nobody should have added";

    std::error_code ec;
    fs::remove_all( root, ec );
}

TEST( ProjectHubTile, TheTimeOnTheTileComesFromTheRegistryAndNotFromNowhere )
{
    const fs::path               root = MakeTempDirectory( "time" );
    Common::Project::ProjectFile descriptor;
    descriptor.Name = "Dunefall";
    ASSERT_TRUE(
         Hub::WriteTextFile( root / "D.deproj", Common::Project::WriteProjectFile( descriptor ) ).IsSuccess() );

    const Common::Project::ProjectRecord record{ ( root / "D.deproj" ).string(), 1757203200 };
    EXPECT_EQ( Hub::ResolveProjectEntry( record ).LastOpened, 1757203200 )
         << "the tile has nothing to draw a relative time from";

    std::error_code ec;
    fs::remove_all( root, ec );
}

// ── the two rules that ARE the behaviour ─────────────────────────────────────────────────────────

TEST( ProjectHubGrid, ColumnsAreFloorOfTheWidthOverTwoFiftyAndNeverFewerThanTwo )
{
    // L2 §6.1 verbatim, and this function IS the resize behaviour — which is why it is a function
    // with a test rather than a line inside a draw loop.
    EXPECT_EQ( Hub::GridColumns( 1002.0f ), 4 ) << "the design width (1280 window) must give four columns";
    EXPECT_EQ( Hub::GridColumns( 622.0f ), 2 ) << "a 900-wide window must give two";
    EXPECT_EQ( Hub::GridColumns( 582.0f ), 2 ) << "the MINIMUM window (860 wide) must still give two";
    EXPECT_EQ( Hub::GridColumns( 750.0f ), 3 );
    EXPECT_EQ( Hub::GridColumns( 999.0f ), 3 ) << "one pixel below the step is still the lower step";
    EXPECT_EQ( Hub::GridColumns( 1000.0f ), 4 ) << "exactly on the step is the higher one";

    // The floor and the minimum are one rule, not a rule plus a rescue: below 500 the floor would
    // say 1 or 0, and the minimum is what a launcher can actually draw a tile in.
    EXPECT_EQ( Hub::GridColumns( 499.0f ), 2 );
    EXPECT_EQ( Hub::GridColumns( 0.0f ), 2 );
    EXPECT_EQ( Hub::GridColumns( -100.0f ), 2 ) << "a degenerate width must not produce a negative column count";
}

TEST( ProjectHubGrid, TheTileStretchesBetweenColumnStepsAndIsCappedSoTwoColumnsAreNotPosters )
{
    // The relation the design states in numbers on screen 04: at 1280 the tile is ~240 wide and 190
    // tall, at 900 it is 303 by 226 — the tile STRETCHES between steps rather than leaving a gutter.
    // Reproduced here from the same two inputs the launcher uses.
    constexpr float gap = 16.0f, cap = 340.0f;
    const auto      tileWidth = [&]( float content )
    {
        const int columns = Hub::GridColumns( content );
        return std::min( cap,
                         ( content - gap * static_cast<float>( columns - 1 ) ) / static_cast<float>( columns ) );
    };
    EXPECT_NEAR( tileWidth( 1002.0f ), 238.5f, 1.0f ) << "screen 04 says 240 at the design width";
    EXPECT_NEAR( tileWidth( 622.0f ), 303.0f, 1.0f ) << "screen 04 says 303 at 900 wide";
    EXPECT_NEAR( tileWidth( 582.0f ), 283.0f, 1.0f ) << "the minimum window";
    // The cap: without it a very wide two-column window grows poster tiles.
    EXPECT_FLOAT_EQ( tileWidth( 720.0f ), cap ) << "the stretch is not capped";
}

TEST( ProjectHubTime, ARelativeTimeIsDrawnFromTheRegistryAndZeroDrawsNothing )
{
    constexpr long long kNow = 1757203200; // a fixed instant: this test owns its own clock

    EXPECT_EQ( Hub::RelativeTime( 0, kNow ), "" )
         << "an entry the registry has no time for must show nothing, not a date computed from zero";
    EXPECT_EQ( Hub::RelativeTime( kNow + 5000, kNow ), "" )
         << "a clock that moved backwards is not a project opened in the future";

    EXPECT_EQ( Hub::RelativeTime( kNow - 30, kNow ), "Just now" );
    EXPECT_EQ( Hub::RelativeTime( kNow - 60, kNow ), "1 minute ago" );
    EXPECT_EQ( Hub::RelativeTime( kNow - 7200, kNow ), "2 hours ago" );
    EXPECT_EQ( Hub::RelativeTime( kNow - 3600, kNow ), "1 hour ago" ) << "the singular is not 1 hours";
    EXPECT_EQ( Hub::RelativeTime( kNow - 30 * 3600, kNow ), "Yesterday" );
    EXPECT_EQ( Hub::RelativeTime( kNow - 3 * 24 * 3600, kNow ), "3 days ago" );

    // Past a week a relative phrase stops being informative, so it becomes a date. The exact
    // spelling is the host's locale month name, so assert the SHAPE: not a "days ago" phrase, and
    // not empty.
    const std::string old = Hub::RelativeTime( kNow - 40LL * 24 * 3600, kNow );
    EXPECT_FALSE( old.empty() );
    EXPECT_EQ( old.find( "days ago" ), std::string::npos ) << "40 days ago is not a fact anyone uses: " << old;
}

// ── which engine, and how it was found ───────────────────────────────────────────────────────────

TEST( ProjectHubEngine, TheRegistryIsPreferredAndTheEnvironmentIsTheBridge )
{
    // engines.json is the durable answer, written by the Editor. DESERT_ROOT is the run script's
    // answer and stops existing for the launcher the moment it moves to its own repository — it is
    // a bridge with an end date, not a second source of truth, so it must lose to the registry.
    const fs::path present = MakeTempDirectory( "engine" );

    Common::Engine::EngineRegistry registry;
    Common::Engine::RegisterInstall( registry, { present.string(), "0.1.492+abc", 492 } );

    const Hub::EngineChoice chosen = Hub::ChooseEngine( registry, "/somewhere/else" );
    EXPECT_EQ( chosen.Root, present.string() ) << "the environment beat the registry";
    EXPECT_EQ( chosen.VersionFull, "0.1.492+abc" );
    EXPECT_FALSE( chosen.Explanation.empty() ) << "the sidebar has nothing to say about where this came from";

    std::error_code ec;
    fs::remove_all( present, ec );
}

TEST( ProjectHubEngine, AnEngineThatIsNoLongerOnDiskIsNotOfferedAsTheOneThatWillStart )
{
    // The same disease as a dead project entry, one level up: agent worktrees get reclaimed, and a
    // root registered from one of them is a path to nothing.
    Common::Engine::EngineRegistry registry;
    Common::Engine::RegisterInstall( registry, { "/gone/worktree/DesertEngine", "0.1.400", 400 } );

    const Hub::EngineChoice fellBack = Hub::ChooseEngine( registry, "/still/here" );
    EXPECT_EQ( fellBack.Root, "/still/here" );

    const Hub::EngineChoice nothing = Hub::ChooseEngine( registry, nullptr );
    EXPECT_EQ( nothing.Root, "" );
    EXPECT_NE( nothing.Explanation.find( "/gone/worktree/DesertEngine" ), std::string::npos )
         << "no engine, and no explanation either: " << nothing.Explanation;
}

TEST( ProjectHubEngine, NoEngineAnywhereIsAStateWithAReasonAndNotAnEmptyString )
{
    const Hub::EngineChoice nothing = Hub::ChooseEngine( Common::Engine::EngineRegistry{}, nullptr );
    EXPECT_EQ( nothing.Root, "" );
    EXPECT_FALSE( nothing.Explanation.empty() )
         << "\"not found\" with no \"and here is where I looked\" is not actionable";
    EXPECT_NE( nothing.Explanation.find( "engines.json" ), std::string::npos ) << nothing.Explanation;
}

// ── the thumbnail budget ─────────────────────────────────────────────────────────────────────────

namespace
{
    // The smallest legal PNG stb will decode: 1x1, written by hand so the suite needs no encoder.
    void WriteTinyPng( const fs::path& file )
    {
        static const unsigned char kPng[] = {
             0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
             0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
             0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78,
             0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99,
             0x3D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        std::ofstream out( file, std::ios::binary );
        out.write( reinterpret_cast<const char*>( kPng ), sizeof( kPng ) );
    }

    Hub::TextureBackend CountingBackend( int& uploads )
    {
        // No graphics API anywhere in this test — which is the point of the cache taking its backend
        // as two callbacks instead of naming one.
        Hub::TextureBackend backend;
        backend.Upload = [&uploads]( const std::uint8_t*, int, int ) -> Hub::TextureHandle
        {
            ++uploads;
            return reinterpret_cast<Hub::TextureHandle>( static_cast<std::uintptr_t>( uploads ) );
        };
        backend.Destroy = []( Hub::TextureHandle ) {};
        return backend;
    }
} // namespace

TEST( ProjectHubThumbnails, AtMostTwoFilesAreDecodedPerFrame )
{
    // A 512x288 PNG costs 1-3 ms to decode. Fourteen at once is a visible freeze on the one screen
    // a launcher exists to draw, so the budget is the reason the grid is usable at all — and
    // without this test "at most two per frame" is a comment.
    const fs::path           root = MakeTempDirectory( "budget" );
    std::vector<std::string> files;
    for ( int i = 0; i < 10; ++i )
    {
        const fs::path file = root / ( "t" + std::to_string( i ) + ".png" );
        WriteTinyPng( file );
        files.push_back( file.string() );
    }

    int                 uploads = 0;
    Hub::ThumbnailCache cache;
    cache.SetBackend( CountingBackend( uploads ) );

    cache.BeginFrame();
    int ready = 0;
    for ( const std::string& file : files )
        if ( cache.Get( file ).Ok() )
            ++ready;
    EXPECT_EQ( ready, Hub::ThumbnailCache::kDecodesPerFrame )
         << "ten tiles decoded " << ready << " files in one frame — that is the freeze the budget exists to stop";
    EXPECT_EQ( cache.DecodeCount(), Hub::ThumbnailCache::kDecodesPerFrame );

    // Frame two: the two already loaded cost nothing, and two more arrive.
    cache.BeginFrame();
    ready = 0;
    for ( const std::string& file : files )
        if ( cache.Get( file ).Ok() )
            ++ready;
    EXPECT_EQ( ready, 2 * Hub::ThumbnailCache::kDecodesPerFrame )
         << "a cached texture cost budget it should not have";
    EXPECT_EQ( cache.DecodeCount(), 2 * Hub::ThumbnailCache::kDecodesPerFrame );

    cache.Shutdown();
    std::error_code ec;
    fs::remove_all( root, ec );
}

TEST( ProjectHubThumbnails, AFileTheEditorRewritesIsPickedUpAndOneItCannotDecodeIsNotRetriedForever )
{
    const fs::path root = MakeTempDirectory( "revalidate" );
    const fs::path good = root / "good.png";
    const fs::path bad  = root / "bad.png";
    WriteTinyPng( good );
    std::ofstream( bad ) << "this is not a png";

    int                 uploads = 0;
    Hub::ThumbnailCache cache;
    cache.SetBackend( CountingBackend( uploads ) );

    cache.BeginFrame();
    EXPECT_TRUE( cache.Get( good.string() ).Ok() );
    EXPECT_FALSE( cache.Get( bad.string() ).Ok() );
    EXPECT_EQ( cache.DecodeCount(), 2 );

    // A corrupt file retried every frame would spend the whole budget on the one tile that can
    // never fill, and starve every tile that could have loaded.
    for ( int frame = 0; frame < 5; ++frame )
    {
        cache.BeginFrame();
        EXPECT_FALSE( cache.Get( bad.string() ).Ok() );
    }
    EXPECT_EQ( cache.DecodeCount(), 2 ) << "a file that cannot be decoded is being retried forever";

    // The Editor rewrites the thumbnail on every save. Keyed on path ALONE, the launcher would go
    // on showing the picture from the first time it looked.
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    fs::last_write_time( good, fs::file_time_type::clock::now() + std::chrono::seconds( 5 ) );
    cache.BeginFrame();
    EXPECT_TRUE( cache.Get( good.string() ).Ok() );
    EXPECT_EQ( cache.DecodeCount(), 3 ) << "a rewritten thumbnail was never re-read";

    cache.Shutdown();
    std::error_code ec;
    fs::remove_all( root, ec );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
