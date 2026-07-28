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

    // Listing reconstructs FULL paths under the mount root.
    const auto listed = Common::Utils::VFS::ListFiles( dir / "Assets" );
    ASSERT_EQ( listed.size(), 1u );
    EXPECT_EQ( listed[0], virtualPath );

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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
