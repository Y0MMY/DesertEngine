// The ENGINE's half of the shared-format contract. The conformance suite itself lives in the
// desert-shared submodule (Tests/project_format_test.cpp — compiled into this binary by the
// premake next door, and by the launcher's runner on its side); this file asserts the one relation
// the submodule cannot know about: its content-folder census against the engine's own
// Constants::Path globals. It also provides the gtest main() the shared file deliberately lacks.

#include <gtest/gtest.h>

#include <Common/Core/Constants.hpp>
#include <DesertShared/ProjectFormat.hpp>

#include <filesystem>
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

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
