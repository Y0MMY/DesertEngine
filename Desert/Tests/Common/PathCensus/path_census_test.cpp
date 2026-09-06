// The guard over Constants.hpp's path census: after a project root changes, EVERY derived directory
// must equal its root joined with its census row's relative part — for all rows at once, not for the
// three somebody remembered to spot-check. The defect shape this pins is the one the seventeen-variable
// era invited: a remap that rewrote sixteen paths and forgot the seventeenth looked correct everywhere
// anyone looked. The census makes that unrepresentable by construction; this suite is the tripwire for
// the day someone hand-edits the derivation itself (a special-cased row would pass compilation and fail
// here).
//
// What is deliberately NOT tested: the on-disk .deproj format and the folders a new project is
// scaffolded with — that census lives with the project format and has its own suite.

#include <Common/Core/Constants.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

namespace Path = Common::Constants::Path;
namespace fs   = std::filesystem;

namespace
{
    // Restores the project root, so this suite cannot leak a fake project into whatever runs after it.
    class ProjectRootGuard
    {
    public:
        ProjectRootGuard() : m_Saved( Path::CurrentProjectRoot() )
        {
        }

        ~ProjectRootGuard()
        {
            Path::SetProjectRoot( m_Saved.ProjectDir, m_Saved.AssetsRoot );
        }

        ProjectRootGuard( const ProjectRootGuard& )            = delete;
        ProjectRootGuard& operator=( const ProjectRootGuard& ) = delete;

    private:
        Path::ProjectRootState m_Saved;
    };

    // The relation itself, spelled once: what row `d` must equal under the root pair (projectDir,
    // assetsRoot). This mirrors the documented contract, not the implementation — it recomputes the
    // expected value from the census spec independently of Derive().
    fs::path Expected( Path::ContentDir d, const fs::path& projectDir, const fs::path& assetsRoot )
    {
        const auto&    spec = Path::CONTENT_DIRS[static_cast<std::size_t>( d )];
        const fs::path base = spec.Root == Path::DirRoot::Assets
                                   ? ( projectDir / assetsRoot ).lexically_normal()
                                   : ( projectDir / Path::COOKED_DIR_NAME ).lexically_normal();
        return base / spec.Rel;
    }

    void ExpectAllRowsDerivedFrom( const fs::path& projectDir, const fs::path& assetsRoot )
    {
        for ( std::size_t i = 0; i < Path::CONTENT_DIR_COUNT; ++i )
        {
            const auto d = static_cast<Path::ContentDir>( i );
            EXPECT_EQ( Path::Dir( d ), Expected( d, projectDir, assetsRoot ) )
                 << "census row " << i << " (rel '" << Path::CONTENT_DIRS[i].Rel
                 << "') does not equal its root plus its relative part";
        }
    }
} // namespace

TEST( PathCensus, EveryDerivedPathEqualsItsRootPlusRelativePart )
{
    ProjectRootGuard guard;

    // Two projects that share nothing, opened in sequence — the relation must hold after EACH remap,
    // for every row, or a stale path from the previous project survives into the new one.
    Path::SetProjectRoot( "/ann/work/Game", "Content" );
    ExpectAllRowsDerivedFrom( "/ann/work/Game", "Content" );

    Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );
    ExpectAllRowsDerivedFrom( "/opt/ci/checkout/Other", "Assets" );
}

TEST( PathCensus, NoRowSurvivesARemapPointingAtThePreviousProject )
{
    ProjectRootGuard guard;

    // The forgotten-seventeenth-variable defect, stated directly: after moving to project B, no derived
    // directory may still mention project A. Checked over the whole census, not a sample.
    Path::SetProjectRoot( "/ann/work/Game", "Content" );
    Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );

    for ( std::size_t i = 0; i < Path::CONTENT_DIR_COUNT; ++i )
    {
        const auto d = static_cast<Path::ContentDir>( i );
        EXPECT_EQ( Path::Dir( d ).generic_string().find( "/ann/work/Game" ), std::string::npos )
             << "census row " << i << " still points into the previously opened project";
    }
}

TEST( PathCensus, TheSandboxLayoutIsTheHistoricalOne )
{
    ProjectRootGuard guard;
    Path::ResetToSandbox();

    // Byte-for-byte the spellings the engine shipped with before the census existed. Every asset
    // registry, cooked file and saved scene in the sandbox depends on these exact strings; a census
    // edit that shifts one is a data migration, not a refactor, and must fail here first.
    const std::array<std::pair<const fs::path*, const char*>, 16> expected = { {
         { &Path::ASSETS_PATH, "Resources/Assets/" },
         { &Path::MESH_PATH, "Resources/Assets/Meshes/" },
         { &Path::MATERIAL_PATH, "Resources/Assets/Materials/" },
         { &Path::TEXTUREDIR_PATH, "Resources/Assets/Textures/" },
         { &Path::SKYBOX_PATH, "Resources/Assets/Textures/HDR/" },
         { &Path::SCENE_PATH, "Resources/Assets/Scenes/" },
         { &Path::PREFAB_PATH, "Resources/Assets/Prefabs/" },
         { &Path::SCRIPT_PATH, "Resources/Assets/Scripts/" },
         { &Path::COLLECTIONS_PATH, "Resources/Assets/Collections/" },
         { &Path::CLOUD_NOISE_PATH, "Resources/Assets/Clouds/" },
         { &Path::CLOUD_TYPE_PATH, "Resources/Assets/Clouds/Types/" },
         { &Path::CLOUD_VOLUME_PATH, "Resources/Assets/Clouds/Volumes/" },
         { &Path::CLOUD_LAYOUT_PATH, "Resources/Assets/Clouds/Layouts/" },
         { &Path::COOKED_PATH, "Cooked/" },
         { &Path::MESH_PATH_COOKED, "Cooked/Meshes/" },
         { &Path::TEXTURE_PATH_COOKED, "Cooked/Textures/" },
    } };
    static_assert( expected.size() == Path::CONTENT_DIR_COUNT,
                   "a census row was added without pinning its sandbox spelling here" );

    for ( const auto& [view, spelling] : expected )
        EXPECT_EQ( view->generic_string(), spelling );
}

TEST( PathCensus, ANamedViewIsTheCensusRowItNames )
{
    // The views are references INTO the derived storage, not copies of it — that identity is what lets
    // AssetHandle's root table, the runtime scan roots and the packager's tree census hold a
    // `const fs::path*` and follow a project switch for free. Compare addresses, not spellings: two
    // equal copies would pass a value comparison and silently stop following remaps.
    EXPECT_EQ( &Path::ASSETS_PATH, &Path::Dir( Path::ContentDir::Assets ) );
    EXPECT_EQ( &Path::COOKED_PATH, &Path::Dir( Path::ContentDir::Cooked ) );
    EXPECT_EQ( &Path::CLOUD_LAYOUT_PATH, &Path::Dir( Path::ContentDir::CloudLayout ) );
}

TEST( PathCensus, AStoredPointerFollowsARemap )
{
    ProjectRootGuard guard;

    // The long-lived-table pattern, exercised end to end: a pointer taken before a project opens must
    // read the remapped value afterwards. This is the load-bearing property the census refactor was not
    // allowed to break.
    const fs::path* mesh = &Path::MESH_PATH;

    Path::SetProjectRoot( "/ann/work/Game", "Content" );
    EXPECT_EQ( *mesh, fs::path( "/ann/work/Game/Content" ) / "Meshes/" );

    Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );
    EXPECT_EQ( *mesh, fs::path( "/opt/ci/checkout/Other/Assets" ) / "Meshes/" );
}

TEST( PathCensus, CurrentProjectRootReportsWhatWasSet )
{
    ProjectRootGuard guard;

    // What the test guards above (and every other suite's ProjectRootGuard) depend on: the stored pair
    // is the WHOLE state, so reading it back and re-setting it must restore every derived path.
    Path::SetProjectRoot( "/ann/work/Game", "Content" );
    const Path::ProjectRootState saved = Path::CurrentProjectRoot();
    EXPECT_EQ( saved.ProjectDir, fs::path( "/ann/work/Game" ) );
    EXPECT_EQ( saved.AssetsRoot, fs::path( "Content" ) );

    Path::SetProjectRoot( "/opt/ci/checkout/Other", "Assets" );
    Path::SetProjectRoot( saved.ProjectDir, saved.AssetsRoot );
    ExpectAllRowsDerivedFrom( "/ann/work/Game", "Content" );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
