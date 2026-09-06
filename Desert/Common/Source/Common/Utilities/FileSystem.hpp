#pragma once

#include <iostream>

#if defined( DESERT_PLATFORM_WINDOWS )
#include <windows.h>

// NOTE: This is a workaround for Microsoft macros so that
// we can use names like CreateDirectory, etc
#ifdef CreateDirectory
#undef CreateDirectory
#undef DeleteFile
#undef MoveFile
#undef CopyFile
#undef CreateFile
#undef SetEnvironmentVariable
#undef GetEnvironmentVariable
#endif
#endif // DESERT_PLATFORM_WINDOWS

#include <functional>
#include <filesystem>

#include <Common/Core/ResultStr.hpp>

namespace Common::Utils
{
    class FileSystem
    {
    public:
        [[nodiscard]] static const std::string GetFileName( const std::filesystem::path& filepath );
        [[nodiscard]] static const std::string GetFileName( const std::string& filepath );
        [[nodiscard]] static const std::string
        GetFileNameWithoutExtension( const std::filesystem::path& filepath );
        [[nodiscard]] static const std::filesystem::path
        GetFileNameWithoutExtension_PATH( const std::filesystem::path& filepath );

    public:
        // THE READ PRIMITIVES ARE SOFT ON PURPOSE, AND THE SOFTNESS IS GUARDED BY THE TYPE. A path
        // that resolves neither on disk nor in a mounted .dpak logs the path (LOG_ERROR) and returns
        // a NAMED error carrying that path — it never terminates the process. A primitive cannot
        // know whether the missing file is fatal to its caller, so the policy lives at the call
        // site: every loader answers a failed read through its own error channel (Common::MakeError
        // / LOG_ERROR / a defaults branch), and in a packaged game an abort down here is a
        // guaranteed crash on the player's machine over a single missing asset.
        //
        // WHAT THE RESULT RETURN ACTUALLY BUYS, stated exactly, because an earlier version of this
        // comment promised more than the type delivers. What it buys is that the OLD shape does not
        // compile: `std::string s = ReadFileContent(p)` is rejected outright, so every one of the
        // ~30 call sites in the engine was rewritten by the COMPILER rather than by eye, and a clean
        // full build is the proof that the migration is complete.
        //
        // What it does NOT buy is a guarantee that the caller decided anything. `GetValue()` and
        // `ExtractValue()` hand back a default-constructed T when the result is an error, so an
        // unchecked unwrap still compiles and still yields the silent emptiness §1.4 forbids — one
        // method call away, with no diagnostic. Do not read "returns a Result" as "the compiler has
        // checked this for you"; the check is still yours to write. (`[[nodiscard]]` below catches
        // only a wholly discarded call, and even that is silent in this workspace, which builds
        // every target with -w — see BuildScripts/Workspace.lua.)
        //
        // It does end the old ambiguity this comment used to have to explain away — a genuinely
        // zero-byte file is a SUCCESS holding an empty value, a missing file is an error, and the
        // two are different values instead of one emptiness that only an up-front Exists() could
        // tell apart.
        [[nodiscard]] static Common::ResultStr<std::string>
                          ReadFileContent( const std::filesystem::path& filepath );
        static const void WriteContentToFile( const std::filesystem::path& filepath, const std::string& content );

        // WRITE-THEN-RENAME, for files whose PREVIOUS contents must survive a failed write. The plain
        // primitive above opens the destination with trunc, so the old file is already gone before the
        // first byte lands — a full disk, dropped permissions or a killed process mid-write leaves
        // zero bytes where data used to be (Tools/SceneMigrator destroyed scenes exactly this way).
        // This one writes `<filepath>.tmp` BESIDE the destination (same directory — rename is only
        // atomic within one filesystem, and the system temp dir can be another volume), verifies the
        // stream after the write AND after close (close() is where a buffered failure finally
        // surfaces), and only then renames over the original. Interruption at any step leaves the
        // original untouched; the worst a failure costs is a stray .tmp, which is removed on the way
        // out. The temp name is deliberately FIXED rather than unique-per-process: two concurrent
        // writers then race to a whole file from one of them instead of interleaving into a torn one,
        // and a test can block the temp path to drive the failure branch.
        // Returns false on any failure, after logging which step failed and where — the caller owns
        // the policy (a tool counts it as a failed file, the editor keeps running).
        [[nodiscard]] static bool WriteContentToFileAtomic( const std::filesystem::path& filepath,
                                                            const std::string&           content );

        [[nodiscard]] static Common::ResultStr<std::vector<uint8_t>>
        ReadByteFileContent( const std::filesystem::path& filepath );

        // Every regular file under `root`, from BOTH halves of the content world: the loose files on
        // disk and everything a mounted .dpak holds under that root, deduplicated by absolute
        // normalized path (a loose file overrides its pak twin). A missing root contributes nothing.
        // Every scanner that enumerates content must go through this: the font and icon services each
        // used to walk only the disk half, so a packaged game — where the loose directories do not
        // exist at all — scanned nothing and no text could resolve its font.
        [[nodiscard]] static std::vector<std::filesystem::path>
        ListFilesRecursive( const std::filesystem::path& root );

    public:
        [[nodiscard]] static const std::filesystem::path GetParentPath( const std::filesystem::path& filepath );
        [[nodiscard]] static const std::string           GetFileExtension( const std::filesystem::path& filepath );
        [[nodiscard]] static const uint32_t              GetFileSize( const std::filesystem::path& filepath );
        static bool                                      CreateDirectory( const std::filesystem::path& directory );
        static bool                                      CreateDirectory( const std::string& directory );
        static void                                      CreateFile( const std::string& path );
        static void                                      CreateFile( const std::filesystem::path& path );
        static bool                                      Exists( const std::filesystem::path& filepath );
        static bool                                      Exists( const std::string& filepath );
        static std::string           GetFileDirectoryString( const std::filesystem::path& filepath );
        static std::filesystem::path GetFileDirectory( const std::filesystem::path& filepath );

        // Absolute path of the running executable — for locating content (a .dpak) packaged next to it.
        [[nodiscard]] static std::filesystem::path ExecutablePath();

    public:
        static std::filesystem::path OpenFileDialog( const char* filter = "All\0*.*\0" );
        static std::filesystem::path OpenFolderDialog( const char* initialFolder = "" );
        static std::filesystem::path SaveFileDialog( const char* filter = "All\0*.*\0" );

    public:
        static bool        HasEnvironmentVariable( const std::string& key );
        static bool        SetEnvironmentVariable( const std::string& key, const std::string& value );
        static std::string GetEnvironmentVariable( const std::string& key );
    };
} // namespace Common::Utils