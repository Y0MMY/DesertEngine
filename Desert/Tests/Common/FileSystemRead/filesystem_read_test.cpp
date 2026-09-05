// The read primitives' contract: a path that resolves neither on disk nor in a mounted .dpak is a
// NAMED failure — the path is logged and an empty result comes back — never process death. Until
// 2026-09-05 both primitives hit DESERT_VERIFY (LogError + abort in EVERY configuration) on that
// miss, which made every "file is empty or missing" branch in the asset loaders dead code and
// crashed the editor over a scene naming a deleted prefab. The tests in this file are alive only
// because the primitives return: reverting the miss path to DESERT_VERIFY kills the test process,
// which is exactly the failure mode the suite pins.
//
// The suite also covers FileSystem::ListFilesRecursive — the ONE enumeration every content scanner
// shares. Its relation: the union of the loose files on disk and the mounted pak's entries under a
// root, deduplicated so a loose file overrides its pak twin. The font and icon services used to
// walk only the disk half, so a packaged game (no loose directories at all) scanned nothing.

#include <Common/Utilities/FileSystem.hpp>
#include <Common/Utilities/PakFile.hpp>
#include <Common/Utilities/VFS.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    fs::path MakeTempDir( const char* name )
    {
        const fs::path dir = fs::temp_directory_path() / name;
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir;
    }

    void WriteFile( const fs::path& p, const std::string& content )
    {
        fs::create_directories( p.parent_path() );
        std::ofstream out( p, std::ios::binary );
        out << content;
    }

    // Restores the working directory even when an assertion throws out of the test body.
    struct CwdGuard
    {
        fs::path Old = fs::current_path();
        ~CwdGuard()
        {
            std::error_code ec;
            fs::current_path( Old, ec );
        }
    };
} // namespace

TEST( FileSystemRead, MissingFileReturnsEmptyStringInsteadOfDying )
{
    const fs::path missing = MakeTempDir( "desert_fsread_test" ) / "does_not_exist.desce";
    ASSERT_FALSE( fs::exists( missing ) );

    // The assertion is not just the emptiness — it is that this line RETURNS at all.
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( missing ), "" );
}

TEST( FileSystemRead, MissingFileReturnsEmptyBytesInsteadOfDying )
{
    const fs::path missing = MakeTempDir( "desert_fsread_test" ) / "does_not_exist.ttf";
    ASSERT_FALSE( fs::exists( missing ) );

    EXPECT_TRUE( Common::Utils::FileSystem::ReadByteFileContent( missing ).empty() );
}

TEST( FileSystemRead, MissingFileUnderAMountedPakIsStillSoft )
{
    // A pak IS mounted and owns the path's directory — the file just is not in it. Both halves of
    // the lookup miss, which is the exact spot the old abort lived in.
    const fs::path dir = MakeTempDir( "desert_fsread_pak" );
    {
        Common::Utils::PakWriter writer( dir / "Content.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        const std::string other = "present";
        ASSERT_TRUE( writer.AddData( "Assets/present.txt", other.data(), other.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }
    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Content.dpak" ) );

    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( dir / "Assets/absent.txt" ), "" );
    EXPECT_TRUE( Common::Utils::FileSystem::ReadByteFileContent( dir / "Assets/absent.txt" ).empty() );

    Common::Utils::VFS::Unmount();
}

TEST( FileSystemRead, ListFilesRecursiveMergesDiskAndPakAndTheLooseFileWins )
{
    const fs::path dir = MakeTempDir( "desert_fsread_list" );

    // Loose half: a.ttf on disk. Pak half: the SAME a.ttf plus b.ttf that exists nowhere loose.
    WriteFile( dir / "Fonts" / "a.ttf", "loose-a" );
    {
        Common::Utils::PakWriter writer( dir / "Content.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        const std::string a = "pak-a", b = "pak-b";
        ASSERT_TRUE( writer.AddData( "Fonts/a.ttf", a.data(), a.size() ) );
        ASSERT_TRUE( writer.AddData( "Fonts/b.ttf", b.data(), b.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }
    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Content.dpak" ) );

    const auto listed = Common::Utils::FileSystem::ListFilesRecursive( dir / "Fonts" );
    ASSERT_EQ( listed.size(), 2u ); // a.ttf deduplicated across the two halves, b.ttf from the pak

    const auto a =
         std::find_if( listed.begin(), listed.end(), []( const fs::path& p ) { return p.filename() == "a.ttf"; } );
    ASSERT_NE( a, listed.end() );
    // The loose spelling survived the dedup, so a later read of the listed path gets the LOOSE bytes
    // (disk-first VFS override) — the relation the debugging workflow relies on.
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( *a ), "loose-a" );

    Common::Utils::VFS::Unmount();
}

TEST( FileSystemRead, ListFilesRecursiveResolvesARelativeRootThroughThePak )
{
    // The packaged-game shape: the launcher cds next to Content.dpak and every resource root is a
    // RELATIVE path ("Resources/Fonts/") with no loose directory behind it. The enumeration must
    // resolve that root against the cwd and find the pak's files.
    const fs::path dir = MakeTempDir( "desert_fsread_rel" );
    {
        Common::Utils::PakWriter writer( dir / "Content.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        const std::string ttf = "packed-font";
        ASSERT_TRUE( writer.AddData( "Resources/Fonts/fake.ttf", ttf.data(), ttf.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    CwdGuard cwd;
    fs::current_path( dir );
    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Content.dpak" ) );
    ASSERT_FALSE( fs::exists( "Resources/Fonts" ) ); // nothing loose — the pak is the only source

    const auto listed = Common::Utils::FileSystem::ListFilesRecursive( "Resources/Fonts/" );
    ASSERT_EQ( listed.size(), 1u );
    EXPECT_EQ( listed[0].filename(), "fake.ttf" );
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( listed[0] ), "packed-font" );

    Common::Utils::VFS::Unmount();
}

TEST( FileSystemRead, ListFilesRecursiveMissingRootIsEmptyNotAnError )
{
    const fs::path dir = MakeTempDir( "desert_fsread_norvoot" );
    EXPECT_TRUE( Common::Utils::FileSystem::ListFilesRecursive( dir / "never_created" ).empty() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
