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
        // THE READ PRIMITIVES ARE SOFT ON PURPOSE. A path that resolves neither on disk nor in a
        // mounted .dpak logs the path (LOG_ERROR) and returns EMPTY — it never terminates the
        // process. A primitive cannot know whether the missing file is fatal to its caller, so the
        // policy lives at the call site: every loader in this engine answers an empty read through
        // its own error channel (Common::MakeError / LOG_ERROR / a defaults branch), and in a
        // packaged game an abort down here is a guaranteed crash on the player's machine over a
        // single missing asset. A caller for which the file IS load-bearing must check the result
        // (or Exists()) and refuse through its own channel — see RuntimeLayer's boot-scene load.
        // NOTE an empty return is also what a genuinely zero-byte file produces; callers that must
        // tell the two apart ask Exists() first.
        [[nodiscard]] static const std::string ReadFileContent( const std::filesystem::path& filepath );
        static const void WriteContentToFile( const std::filesystem::path& filepath, const std::string& content );

        [[nodiscard]] static std::vector<uint8_t> ReadByteFileContent( const std::filesystem::path& filepath );

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