// The RELATION under test: what one process writes about a project, another process reads back
// unchanged. Three processes share these formats — the Project Hub creates a .deproj and maintains
// projects.json, the Editor/Runtime open both through Engine/Project/ProjectContext, and the
// GamePackager regenerates the .deproj it ships. Until 2026-09 each writer was a hand-spliced JSON
// string and only a comment ("keep the field name in sync") held the sides together; every test
// here would have been unwritable, because there was no shared code to point it at.
//
// Two axes of drift are pinned:
//   1. producer vs consumer — round trips through the ONE serializer both sides now call;
//   2. binary vs history — golden JSON literals: a .deproj written last month must still open, so
//      renaming a struct member (which rfl reflects into the file) turns these tests red instead of
//      orphaning every project on disk.

#include <gtest/gtest.h>

#include <Common/Project/ProjectFormat.hpp>

#include <filesystem>

using namespace Common::Project;

namespace
{
    ProjectFile ReadOk( const std::string& json )
    {
        auto result = ReadProjectFile( json );
        EXPECT_TRUE( result.IsSuccess() ) << "expected a parse, got: " << result.GetError();
        return result.ExtractValue();
    }
} // namespace

// ── producer vs consumer ─────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, WhatTheHubWritesTheEngineReads )
{
    // Exactly what the hub's New Project flow produces, including the characters the old
    // string-splice could not survive: a quote and a backslash in the project name.
    ProjectFile written;
    written.Name         = "Neo \"Dune\" \\ 2";
    written.DefaultScene = written.AssetsRoot + "/Scenes/Main.desce";

    const ProjectFile read = ReadOk( WriteProjectFile( written ) );
    EXPECT_EQ( read.Name, written.Name );
    EXPECT_EQ( read.AssetsRoot, written.AssetsRoot );
    EXPECT_EQ( read.DefaultScene, written.DefaultScene );
}

TEST( ProjectFormat, RegistryRoundTripsAWindowsPath )
{
    // The hub's old reader was a naive quoted-string scanner: it returned the JSON escapes RAW, so
    // every backslash the Editor had escaped came back doubled and the path pointed nowhere.
    ProjectsRegistry written;
    written.Projects = { "C:\\Users\\dev\\My Game\\MyGame.deproj", "/Users/dev/Other/Other.deproj" };

    auto read = ReadProjectsRegistry( WriteProjectsRegistry( written ) );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    EXPECT_EQ( read.GetValue().Projects, written.Projects );
}

// ── binary vs history ────────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, DeprojFieldNamesArePinnedToWhatIsAlreadyOnDisk )
{
    // A verbatim .deproj as every binary before this one wrote it. Renaming a ProjectFile member
    // must fail HERE, not on a user's project folder.
    const ProjectFile read =
         ReadOk( R"({"Name":"MyGame","AssetsRoot":"Assets","DefaultScene":"Assets/Scenes/MyGame.desce"})" );
    EXPECT_EQ( read.Name, "MyGame" );
    EXPECT_EQ( read.AssetsRoot, "Assets" );
    EXPECT_EQ( read.DefaultScene, "Assets/Scenes/MyGame.desce" );
}

TEST( ProjectFormat, WriterEmitsThePinnedFieldNames )
{
    const std::string json = WriteProjectFile( ProjectFile{} );
    EXPECT_NE( json.find( "\"Name\"" ), std::string::npos ) << json;
    EXPECT_NE( json.find( "\"AssetsRoot\"" ), std::string::npos ) << json;
    EXPECT_NE( json.find( "\"DefaultScene\"" ), std::string::npos ) << json;
}

TEST( ProjectFormat, RegistryFieldNameIsPinned )
{
    auto read = ReadProjectsRegistry( R"({"Projects":["/a/A.deproj","/b/B.deproj"]})" );
    ASSERT_TRUE( read.IsSuccess() ) << read.GetError();
    ASSERT_EQ( read.GetValue().Projects.size(), 2u );
    EXPECT_EQ( read.GetValue().Projects[0], "/a/A.deproj" );

    const std::string json = WriteProjectsRegistry( ProjectsRegistry{} );
    EXPECT_NE( json.find( "\"Projects\"" ), std::string::npos ) << json;
}

// ── refusals are named ───────────────────────────────────────────────────────────────────────────

TEST( ProjectFormat, GarbageIsRefusedWithAReason )
{
    auto result = ReadProjectFile( "this is not json" );
    ASSERT_FALSE( result.IsSuccess() );
    EXPECT_FALSE( result.GetError().empty() );
}

TEST( ProjectFormat, AMissingFieldIsRefusedNotDefaulted )
{
    // rfl requires every non-optional member, so all three fields are mandatory ON DISK even though
    // two have C++ defaults. That is fine while every producer goes through WriteProjectFile (it
    // always emits all three) — but it means the C++ defaults never apply during a read. If a
    // hand-authored minimal .deproj should ever open, this is the test to renegotiate.
    EXPECT_FALSE( ReadProjectFile( R"({"Name":"X"})" ).IsSuccess() );
    EXPECT_FALSE( ReadProjectsRegistry( R"({})" ).IsSuccess() );
}

// ── the folder census vs the engine's constants ──────────────────────────────────────────────────

TEST( ProjectFormat, CensusRowsMatchTheConstantsTheEngineReads )
{
    // The census says "a project has <assetsRoot>/Meshes/"; the engine reads content through
    // Constants::Path::MESH_PATH. These are two sides of one fact, so assert the RELATION: after
    // SetProjectRoot, every census row's engine constant is exactly assetsRoot / RelativePath.
    //
    // Runs LAST in this file on purpose: SetProjectRoot mutates the process-wide path globals.
    namespace fs                 = std::filesystem;
    const fs::path    projectDir = fs::path( "/tmp/desert-project-format-test" );
    const ProjectFile deproj; // the default AssetsRoot every producer writes
    Common::Constants::Path::SetProjectRoot( projectDir, deproj.AssetsRoot );

    const fs::path assets = ( projectDir / deproj.AssetsRoot ).lexically_normal();
    for ( const auto& folder : StandardContentFolders )
    {
        ASSERT_NE( folder.EnginePath, nullptr );
        EXPECT_EQ( *folder.EnginePath, assets / folder.RelativePath )
             << "census row '" << folder.RelativePath << "' no longer matches the engine constant it is tied to";
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
