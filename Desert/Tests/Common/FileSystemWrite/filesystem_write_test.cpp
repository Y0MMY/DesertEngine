// The atomic write primitive's contract: WriteContentToFileAtomic either lands the WHOLE content or
// leaves the destination BYTE-IDENTICAL, and says which happened in its return value. The plain
// WriteContentToFile cannot promise this — it opens the destination with trunc, so the old contents
// are gone before the first new byte lands, and an interruption (full disk, dropped permissions, a
// killed process) leaves zero bytes where data used to be. Tools/SceneMigrator destroyed scenes
// exactly that way, and the shared recent-projects registry could be torn by either of its two
// writers; both go through this primitive now.
//
// The discriminating tests below are the ones that FAIL against an in-place implementation: they
// build situations where writing the destination directly would succeed (and destroy it) while the
// temp-then-rename path is refused — so reverting the primitive to trunc-in-place turns them red,
// which is the mutation check the fix shipped with.

#include <Common/Utilities/FileSystem.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

using Common::Utils::FileSystem;

namespace
{
    fs::path MakeTempDir( const char* name )
    {
        const fs::path dir = fs::temp_directory_path() / name;
        fs::remove_all( dir );
        fs::create_directories( dir );
        return dir;
    }

    void WriteRaw( const fs::path& p, const std::string& content )
    {
        std::ofstream out( p, std::ios::binary );
        out << content;
    }

    std::string ReadRaw( const fs::path& p )
    {
        std::ifstream      in( p, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }
} // namespace

// The happy path: the content lands whole, and the working file the primitive wrote through is gone.
// A stray .tmp beside every saved file would read as litter, and worse — a crash-then-restart could
// mistake a stale one for in-flight work.
TEST( FileSystemWrite, ASuccessfulWriteLandsWholeAndLeavesNoTemporaryBehind )
{
    const fs::path dir  = MakeTempDir( "desert_fs_write_success" );
    const fs::path file = dir / "out.json";

    EXPECT_TRUE( FileSystem::WriteContentToFileAtomic( file, "{\"a\":1}" ) );

    EXPECT_EQ( ReadRaw( file ), "{\"a\":1}" );
    fs::path temp = file;
    temp += ".tmp";
    EXPECT_FALSE( fs::exists( temp ) ) << "the working file survived the rename";

    fs::remove_all( dir );
}

// Replacing an existing file is the primitive's whole reason to exist — every caller (the scene
// migrator, both registry writers) overwrites. The new content must REPLACE, not append to or merge
// with, the old.
TEST( FileSystemWrite, AnExistingFileIsReplacedWithTheNewContentExactly )
{
    const fs::path dir  = MakeTempDir( "desert_fs_write_replace" );
    const fs::path file = dir / "out.json";
    WriteRaw( file, "the old contents, deliberately longer than the new ones" );

    EXPECT_TRUE( FileSystem::WriteContentToFileAtomic( file, "short" ) );
    EXPECT_EQ( ReadRaw( file ), "short" );

    fs::remove_all( dir );
}

// DISCRIMINATING TEST 1 (all platforms): the temp path is blocked by a directory, so the primitive
// cannot even begin — and the destination itself is perfectly writable, so an in-place trunc
// implementation would sail through and replace it. Green only when the failure is refused BEFORE
// the original is touched.
TEST( FileSystemWrite, ABlockedTemporaryCostsTheWriteAndNotTheOriginal )
{
    const fs::path dir  = MakeTempDir( "desert_fs_write_blocked" );
    const fs::path file = dir / "out.json";
    WriteRaw( file, "{\"precious\":true}" );

    fs::path temp = file;
    temp += ".tmp";
    fs::create_directories( temp ); // a directory where the primitive needs its working file

    EXPECT_FALSE( FileSystem::WriteContentToFileAtomic( file, "{\"replacement\":true}" ) );
    EXPECT_EQ( ReadRaw( file ), "{\"precious\":true}" ) << "a failed write cost the original its contents";

    fs::remove_all( dir );
}

#ifndef _WIN32
// DISCRIMINATING TEST 2 (POSIX): the directory is read-only but the file inside it stays writable —
// the exact permissions shape under which the in-place implementation SUCCEEDS (opening an existing
// file for write needs no directory write permission) while creating the temp beside it is refused.
// Windows is excluded because its read-only directory attribute does not deny file creation, so the
// situation cannot be built there with std::filesystem; the blocked-temp test above covers Windows.
TEST( FileSystemWrite, AReadOnlyDirectoryCostsTheWriteAndNotTheOriginal )
{
    const fs::path dir  = MakeTempDir( "desert_fs_write_readonly" );
    const fs::path file = dir / "out.json";
    WriteRaw( file, "{\"precious\":true}" );

    fs::permissions( dir, fs::perms::owner_read | fs::perms::owner_exec );

    EXPECT_FALSE( FileSystem::WriteContentToFileAtomic( file, "{\"replacement\":true}" ) );
    EXPECT_EQ( ReadRaw( file ), "{\"precious\":true}" ) << "a failed write cost the original its contents";

    // Restore before cleanup, or remove_all leaves the read-only directory behind for the next run.
    fs::permissions( dir, fs::perms::owner_all );
    fs::remove_all( dir );
}
#endif

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
