#include <Common/Utilities/PakFile.hpp>
#include <Common/Utilities/VFS.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
    fs::path MakeTempDir()
    {
        const fs::path dir = fs::temp_directory_path() / "desert_pak_test";
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir;
    }
} // namespace

TEST( Pak, WriteReadRoundtrip )
{
    const fs::path dir = MakeTempDir();

    {
        Common::Utils::PakWriter writer( dir / "test.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        const std::string a = "hello pak";
        const std::string b( 100000, 'x' ); // bigger-than-one-block blob
        ASSERT_TRUE( writer.AddData( "Assets/a.txt", a.data(), a.size() ) );
        ASSERT_TRUE( writer.AddData( "Cooked/Meshes/b.stmesh", b.data(), b.size() ) );
        EXPECT_EQ( writer.Finalize(), 2u );
    }

    Common::Utils::PakReader reader( dir / "test.dpak" );
    ASSERT_TRUE( reader.IsOpen() );
    EXPECT_EQ( reader.EntryCount(), 2u );
    EXPECT_TRUE( reader.Contains( "Assets/a.txt" ) );
    EXPECT_FALSE( reader.Contains( "Assets/missing.txt" ) );

    auto a = reader.Read( "Assets/a.txt" );
    ASSERT_TRUE( a.has_value() );
    EXPECT_EQ( *a, "hello pak" );

    auto b = reader.Read( "Cooked/Meshes/b.stmesh" );
    ASSERT_TRUE( b.has_value() );
    EXPECT_EQ( b->size(), 100000u );
    EXPECT_EQ( ( *b )[99999], 'x' );

    const auto cooked = reader.KeysWithPrefix( "Cooked/" );
    ASSERT_EQ( cooked.size(), 1u );
    EXPECT_EQ( cooked[0], "Cooked/Meshes/b.stmesh" );
}

TEST( Pak, VfsMountResolvesAbsolutePathsAndFileSystemFallsBack )
{
    const fs::path dir = MakeTempDir();

    {
        Common::Utils::PakWriter writer( dir / "Content.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        const std::string scene = "{\"scene\":true}";
        ASSERT_TRUE( writer.AddData( "Assets/Scenes/Main.desce", scene.data(), scene.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Content.dpak" ) );

    // The file does NOT exist on disk — only in the pak. Absolute path under the mount root resolves.
    const fs::path virtualPath = dir / "Assets" / "Scenes" / "Main.desce";
    ASSERT_FALSE( fs::exists( virtualPath ) );

    EXPECT_TRUE( Common::Utils::VFS::Exists( virtualPath ) );
    EXPECT_TRUE( Common::Utils::FileSystem::Exists( virtualPath ) );                 // VFS-aware
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( virtualPath ),            // read via pak
               "{\"scene\":true}" );
    EXPECT_EQ( Common::Utils::FileSystem::GetFileSize( virtualPath ), 14u );

    // Paths outside the mount root stay unresolved.
    EXPECT_FALSE( Common::Utils::VFS::Exists( "/definitely/not/mounted.txt" ) );

    // Listing reconstructs FULL paths under the mount root — in the VFS's ONE canonical spelling
    // (symlinked prefixes resolved: on macOS this temp dir is /var/... as spelled and /private/var/...
    // resolved). The contract is not a spelling but the round trip: a listed path must name the same
    // file as the spelling the caller asked with, and must READ back through the VFS.
    const auto listed = Common::Utils::VFS::ListFiles( dir / "Assets" );
    ASSERT_EQ( listed.size(), 1u );
    std::error_code cec;
    EXPECT_EQ( fs::weakly_canonical( listed[0], cec ), fs::weakly_canonical( virtualPath, cec ) );
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( listed[0] ), "{\"scene\":true}" );

    // LOOSE FILE OVERRIDE: a real file with the same path wins over the pak entry.
    fs::create_directories( virtualPath.parent_path() );
    {
        std::ofstream out( virtualPath );
        out << "loose";
    }
    EXPECT_EQ( Common::Utils::FileSystem::ReadFileContent( virtualPath ), "loose" );

    Common::Utils::VFS::Unmount();
    EXPECT_FALSE( Common::Utils::VFS::Exists( virtualPath ) );
}

TEST( Pak, EntryHashesMatchContent )
{
    const fs::path dir = MakeTempDir();

    const std::string payload = "hash me";
    {
        Common::Utils::PakWriter writer( dir / "hash.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        ASSERT_TRUE( writer.AddData( "a.bin", payload.data(), payload.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    Common::Utils::PakReader reader( dir / "hash.dpak" );
    ASSERT_TRUE( reader.IsOpen() );
    const auto h = reader.EntryHash( "a.bin" );
    ASSERT_TRUE( h.has_value() );
    EXPECT_EQ( *h, Common::Utils::PakContentHash( payload.data(), payload.size() ) );
    EXPECT_NE( *h, 0u );
    EXPECT_FALSE( reader.EntryHash( "missing" ).has_value() );
}

TEST( Pak, PatchMountOverridesBase )
{
    const fs::path dir = MakeTempDir();

    const std::string baseData  = "base";
    const std::string patchData = "patched";
    const std::string extraData = "only-in-base";
    {
        Common::Utils::PakWriter writer( dir / "Content.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        ASSERT_TRUE( writer.AddData( "Assets/a.txt", baseData.data(), baseData.size() ) );
        ASSERT_TRUE( writer.AddData( "Assets/b.txt", extraData.data(), extraData.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }
    {
        Common::Utils::PakWriter writer( dir / "Patch_001.dpak" );
        ASSERT_TRUE( writer.IsOpen() );
        ASSERT_TRUE( writer.AddData( "Assets/a.txt", patchData.data(), patchData.size() ) );
        ASSERT_TRUE( writer.Finalize() > 0 );
    }

    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Content.dpak" ) );
    ASSERT_TRUE( Common::Utils::VFS::MountPak( dir / "Patch_001.dpak" ) ); // later mount wins

    // The patched key reads from the LATER mount; untouched keys still come from the base.
    EXPECT_EQ( Common::Utils::VFS::ReadFile( dir / "Assets/a.txt" ).value_or( "" ), "patched" );
    EXPECT_EQ( Common::Utils::VFS::ReadFile( dir / "Assets/b.txt" ).value_or( "" ), "only-in-base" );

    // Listing dedupes overridden keys (a.txt appears once).
    const auto listed = Common::Utils::VFS::ListFiles( dir / "Assets" );
    EXPECT_EQ( listed.size(), 2u );

    Common::Utils::VFS::Unmount();
    EXPECT_FALSE( Common::Utils::VFS::IsMounted() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
