// The ENGINE's half of the shared-format contract. The conformance suite itself lives in the
// desert-shared submodule (Tests/project_format_test.cpp — compiled into this binary by the
// premake next door, and by the launcher's runner on its side); this file asserts the one relation
// the submodule cannot know about: its content-folder census against the engine's own
// Constants::Path globals. It also provides the gtest main() the shared file deliberately lacks.

#include <gtest/gtest.h>

#include <Common/Core/Constants.hpp>
#include <Common/Core/Version.hpp>
#include <DesertShared/EngineRegistry.hpp>
#include <DesertShared/ProjectFormat.hpp>
#include <Engine/Project/EngineRegistration.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

TEST( ProjectFormatEngine, EveryCensusRowIsAConstantTheEngineReads )
{
    // The census says "a project has <assetsRoot>/Meshes/"; the engine reads content through
    // Constants::Path::MESH_PATH. Two sides of one fact — assert the RELATION: after
    // SetProjectRoot, the constant each row is tied to must be exactly assetsRoot / row. The rows
    // here are a second list on purpose: it is the engine NAMING which of its constants answers
    // for each census entry, so a census edit without an engine-side answer fails right here.
    namespace P  = Common::Constants::Path;
    namespace fs = std::filesystem;

    struct Row
    {
        std::string_view Relative;
        const fs::path*  EnginePath;
    };
    const Row rows[] = {
         { "Meshes/", &P::MESH_PATH },
         { "Materials/", &P::MATERIAL_PATH },
         { "Textures/", &P::TEXTUREDIR_PATH },
         { "Scenes/", &P::SCENE_PATH },
         { "Prefabs/", &P::PREFAB_PATH },
         { "Scripts/", &P::SCRIPT_PATH },
         { "Collections/", &P::COLLECTIONS_PATH },
    };
    ASSERT_EQ( std::size( rows ), Common::Project::StandardContentFolders.size() )
         << "the shared census changed size and the engine has not answered for the new row";

    const fs::path                     projectDir = fs::path( "/tmp/desert-project-format-test" );
    const Common::Project::ProjectFile deproj;          // the default AssetsRoot every producer writes
    P::SetProjectRoot( projectDir, deproj.AssetsRoot ); // mutates process-wide globals — this suite only

    const fs::path assets = ( projectDir / deproj.AssetsRoot ).lexically_normal();
    for ( size_t i = 0; i < std::size( rows ); ++i )
    {
        EXPECT_EQ( rows[i].Relative, Common::Project::StandardContentFolders[i] )
             << "row " << i << ": engine mapping and shared census disagree on the folder itself";
        EXPECT_EQ( *rows[i].EnginePath, assets / rows[i].Relative )
             << "census row '" << rows[i].Relative << "' no longer matches the engine constant it is tied to";
    }
}

// ── the engine's half of engines.json: it is the WRITER, and the launcher only ever reads ───────

namespace
{
    std::filesystem::path TempConfigDirectory( const std::string& label )
    {
        const std::filesystem::path directory =
             std::filesystem::temp_directory_path() /
             ( "desert-engines-" + label + "-" +
               std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );
        std::error_code ec;
        std::filesystem::remove_all( directory, ec );
        std::filesystem::create_directories( directory, ec );
        return directory;
    }

    std::string ReadWhole( const std::filesystem::path& file )
    {
        std::ifstream in( file );
        return std::string( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
    }
} // namespace

TEST( EngineRegistration, TheEngineWritesDownWhereItIsSoTheLauncherCanFindIt )
{
    // After L3 the launcher is a different repository with no DESERT_ROOT exported for it, so this
    // file is its ONLY route to an engine. What the engine writes has to come back out through the
    // shared reader the launcher calls — that relation is the whole point of the format.
    const std::filesystem::path config = TempConfigDirectory( "write" );
    const std::filesystem::path root   = TempConfigDirectory( "root" );

    const auto registered = Desert::Project::RegisterThisEngine( config.string(), root.string() );
    ASSERT_TRUE( registered.IsSuccess() ) << registered.GetError();

    auto read = Common::Engine::ReadEngineRegistry( ReadWhole( config / "engines.json" ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    const Common::Engine::EngineInstall* install = Common::Engine::PreferredInstall( read.GetValue() );
    ASSERT_NE( install, nullptr );
    EXPECT_EQ( install->Root, root.string() );
    // The version is THIS build's, not a literal: a registry that reported someone else's version
    // would send the launcher's compatibility checks the wrong answer.
    EXPECT_EQ( install->VersionFull, Common::Version::Full() );
    EXPECT_EQ( install->CommitCount, static_cast<int>( Common::Version::CommitCount() ) );

    std::error_code ec;
    std::filesystem::remove_all( config, ec );
    std::filesystem::remove_all( root, ec );
}

TEST( EngineRegistration, StartingTheSameEngineAgainDoesNotGrowTheFile )
{
    // The Editor registers on EVERY start, and a developer's tree is one root opened a thousand
    // times. An append would hand the launcher a thousand identical sidebar entries.
    const std::filesystem::path config = TempConfigDirectory( "twice" );
    const std::filesystem::path root   = TempConfigDirectory( "root2" );

    ASSERT_TRUE( Desert::Project::RegisterThisEngine( config.string(), root.string() ).IsSuccess() );
    ASSERT_TRUE( Desert::Project::RegisterThisEngine( config.string(), root.string() ).IsSuccess() );

    auto read = Common::Engine::ReadEngineRegistry( ReadWhole( config / "engines.json" ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    EXPECT_EQ( read.GetValue().Engines.size(), 1u ) << "the same root was registered twice";

    std::error_code ec;
    std::filesystem::remove_all( config, ec );
    std::filesystem::remove_all( root, ec );
}

TEST( EngineRegistration, ARegistryThisBuildCannotParseIsLeftUntouchedRatherThanOverwritten )
{
    // The file belongs to the USER: it may hold an engine this build knows nothing about, written
    // by a newer one. Rewriting it from scratch would silently delete that engine from the
    // launcher's sidebar every time this one started.
    const std::filesystem::path config   = TempConfigDirectory( "corrupt" );
    const std::filesystem::path root     = TempConfigDirectory( "root3" );
    const std::string           original = R"({"Engines": this is not json)";
    {
        std::ofstream out( config / "engines.json" );
        out << original;
    }

    const auto registered = Desert::Project::RegisterThisEngine( config.string(), root.string() );
    EXPECT_FALSE( registered.IsSuccess() ) << "a file that could not be parsed was reported as written";
    EXPECT_NE( registered.GetError().find( "engines.json" ), std::string::npos ) << registered.GetError();
    EXPECT_EQ( ReadWhole( config / "engines.json" ), original )
         << "the user's registry was overwritten by a build that could not read it";

    std::error_code ec;
    std::filesystem::remove_all( config, ec );
    std::filesystem::remove_all( root, ec );
}

TEST( EngineRegistration, WithNoEngineRootThereIsNothingToRegisterAndItSaysWhy )
{
    const std::filesystem::path config     = TempConfigDirectory( "noroot" );
    const auto                  registered = Desert::Project::RegisterThisEngine( config.string(), "" );
    ASSERT_FALSE( registered.IsSuccess() );
    EXPECT_NE( registered.GetError().find( "DESERT_ROOT" ), std::string::npos ) << registered.GetError();
    EXPECT_FALSE( std::filesystem::exists( config / "engines.json" ) )
         << "an empty registry was written for an engine that was never located";

    std::error_code ec;
    std::filesystem::remove_all( config, ec );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
