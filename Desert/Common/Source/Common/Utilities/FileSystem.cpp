#include <Common/Utilities/FileSystem.hpp>
#include "VFS.hpp"

#if defined( DESERT_PLATFORM_WINDOWS )
#include <Common/Platform/Windows/WindowsFileSystem.hpp>
#elif defined( DESERT_PLATFORM_MACOS )
#include <Common/Platform/MacOS/MacOSFileSystem.hpp>
#include <mach-o/dyld.h>
#endif

#include <Common/Core/Core.hpp>

#include <filesystem>
#include <fstream>
#include <unordered_set>

#ifdef _MSC_VER
#pragma warning( error : 4834 )
#endif

namespace fs = std::filesystem;

namespace Common::Utils
{
    class WindowsFileSystem;
    bool FileSystem::CreateDirectory( const std::filesystem::path& directory )
    {
        return fs::create_directory( directory );
    }

    bool FileSystem::CreateDirectory( const std::string& directory )
    {
        return CreateDirectory( fs::path( directory ) );
    }

    void FileSystem::CreateFile( const std::string& path )
    {
        CreateFile( fs::path( path ) );
    }

    void FileSystem::CreateFile( const std::filesystem::path& path )
    {
        std::ofstream file( path );

        if ( file.is_open() )
        {
            LOG_INFO( "Created File {}", path.string() );
            file.close();
        }
        else
        {
            // Same soft contract as the read primitives: name the failure, let the caller decide.
            LOG_ERROR( "[FileSystem] Could not create file: {}", path.string() );
        }
    }

    bool FileSystem::Exists( const std::filesystem::path& filepath )
    {
        return fs::exists( filepath ) || VFS::Exists( filepath );
    }

    bool FileSystem::Exists( const std::string& filepath )
    {
        return Exists( fs::path( filepath ) );
    }

    const std::string FileSystem::GetFileName( const std::filesystem::path& filepath )
    {
        return filepath.filename().string();
    }

    const std::string FileSystem::GetFileName( const std::string& filepath )
    {
        return std::filesystem::path( filepath ).filename().string();
    }

    std::filesystem::path FileSystem::OpenFileDialog( const char* filter )
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        return WindowsFileSystem::OpenFileDialog( filter );
#elif defined( DESERT_PLATFORM_MACOS )
        return MacOSFileSystem::OpenFileDialog( filter );
#else
        (void)filter;
        return {};
#endif
    }

    std::filesystem::path FileSystem::OpenFolderDialog( const char* initialFolder )
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        return WindowsFileSystem::OpenFolderDialog( initialFolder );
#elif defined( DESERT_PLATFORM_MACOS )
        return MacOSFileSystem::OpenFolderDialog( initialFolder );
#else
        (void)initialFolder;
        return {};
#endif
    }

    std::filesystem::path FileSystem::SaveFileDialog( const char* filter )
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        return WindowsFileSystem::SaveFileDialog( filter );
#elif defined( DESERT_PLATFORM_MACOS )
        return MacOSFileSystem::SaveFileDialog( filter );
#else
        (void)filter;
        return {};
#endif
    }

    std::filesystem::path FileSystem::GetFileDirectory( const std::filesystem::path& filepath )
    {
        return filepath.parent_path();
    }

    std::filesystem::path FileSystem::ExecutablePath()
    {
#if defined( DESERT_PLATFORM_WINDOWS )
        wchar_t     buf[MAX_PATH];
        const DWORD n = ::GetModuleFileNameW( nullptr, buf, MAX_PATH );
        return fs::path( std::wstring( buf, n ) );
#elif defined( DESERT_PLATFORM_MACOS )
        uint32_t size = 0;
        _NSGetExecutablePath( nullptr, &size ); // first call: query required buffer size
        std::string buf( size, '\0' );
        if ( _NSGetExecutablePath( buf.data(), &size ) != 0 )
            return {};
        std::error_code ec;
        const fs::path  p     = fs::path( buf.c_str() );
        const fs::path  canon = fs::weakly_canonical( p, ec ); // resolve symlinks / '..'
        return ec ? p : canon;
#else // Linux and other POSIX
        std::error_code ec;
        const fs::path  p = fs::read_symlink( "/proc/self/exe", ec );
        return ec ? fs::path{} : p;
#endif
    }

    std::string FileSystem::GetFileDirectoryString( const std::filesystem::path& filepath )
    {
        return filepath.parent_path().string();
    }

    const std::string FileSystem::ReadFileContent( const std::filesystem::path& filepath )
    {
        std::ifstream in( filepath, std::ios::in | std::ios::binary );
        if ( !in )
        {
            // Not on disk: a packaged game serves content from the mounted .dpak (disk first so loose
            // files can still override archive entries while debugging a package).
            if ( auto packed = VFS::ReadFile( filepath ) )
                return std::move( *packed );

            // Soft by contract (see the header): the caller owns the policy for a missing file. This
            // used to DESERT_VERIFY, i.e. abort in every configuration — which made every "file is
            // empty or missing" branch in the loaders dead code and turned one missing asset into a
            // crash of a packaged game.
            LOG_ERROR( "[FileSystem] Could not read file (not on disk, not in a mounted pak): {}",
                       filepath.string() );
            return {};
        }

        std::string fileContent;

        in.seekg( 0, std::ios::end );
        fileContent.resize( in.tellg() );
        in.seekg( 0, std::ios::beg );
        in.read( &fileContent[0], fileContent.size() );
        in.close();

        return fileContent;
    }

    std::vector<uint8_t> FileSystem::ReadByteFileContent( const std::filesystem::path& filepath )
    {
        std::ifstream file( filepath, std::ios::in | std::ios::binary );
        if ( !file )
        {
            if ( auto packed = VFS::ReadFile( filepath ) )
                return std::vector<uint8_t>( packed->begin(), packed->end() );

            // Soft by contract (see the header) — same reasoning as ReadFileContent above.
            LOG_ERROR( "[FileSystem] Could not open file (not on disk, not in a mounted pak): {}",
                       filepath.string() );
            return {};
        }

        file.seekg( 0, std::ios::end );
        std::streamsize fileSize = file.tellg();
        file.seekg( 0, std::ios::beg );

        std::vector<uint8_t> binaryData( fileSize / sizeof( uint8_t ) );
        if ( !file.read( reinterpret_cast<char*>( binaryData.data() ), fileSize ) )
        {
            LOG_ERROR( "[FileSystem] Could not read {} bytes of file: {}", fileSize, filepath.string() );
            return {};
        }
        return std::move( binaryData );
    }

    std::vector<std::filesystem::path> FileSystem::ListFilesRecursive( const std::filesystem::path& root )
    {
        std::vector<fs::path> result;

        // Dedup key = absolute, symlink-resolved path — the same canonical spelling the VFS resolves
        // against — so a relative disk spelling and the pak's absolute one collapse into ONE
        // candidate, and the loose file (pushed first) is the spelling that survives. weakly_canonical
        // and not lexically_normal alone, because the disk walk yields the root as SPELLED while
        // VFS::ListFiles yields the mount root as RESOLVED, and under a symlinked prefix (macOS
        // /var -> /private/var) those are two spellings of one file.
        std::unordered_set<std::string> seen;
        std::error_code                 ec;
        const fs::path                  cwd  = fs::current_path( ec );
        auto                            push = [&]( const fs::path& p )
        {
            const fs::path  raw = ( p.is_absolute() ? p : cwd / p ).lexically_normal();
            std::error_code canonEc;
            fs::path        abs = fs::weakly_canonical( raw, canonEc );
            if ( canonEc || abs.empty() )
                abs = raw;
            if ( seen.insert( abs.generic_string() ).second )
                result.push_back( p );
        };

        if ( fs::exists( root, ec ) ) // a missing root is a valid state (clean project, packaged game)
        {
            for ( auto it = fs::recursive_directory_iterator( root, ec ); it != fs::recursive_directory_iterator();
                  it.increment( ec ) )
            {
                if ( ec )
                    break;
                if ( it->is_regular_file( ec ) )
                    push( it->path() );
            }
        }
        for ( const auto& packed : VFS::ListFiles( root ) )
            push( packed );
        return result;
    }

    const std::filesystem::path FileSystem::GetParentPath( const std::filesystem::path& filepath )
    {
        return filepath.parent_path();
    }

    const std::string FileSystem::GetFileNameWithoutExtension( const std::filesystem::path& filepath )
    {
        return GetFileNameWithoutExtension_PATH( filepath ).string();
    }

    const std::filesystem::path
    FileSystem::GetFileNameWithoutExtension_PATH( const std::filesystem::path& filepath )
    {
        return filepath.stem();
    }

    const std::string FileSystem::GetFileExtension( const std::filesystem::path& filepath )
    {
        return filepath.extension().string();
    }

    const uint32_t FileSystem::GetFileSize( const std::filesystem::path& filepath )
    {
        std::error_code ec;
        if ( fs::exists( filepath, ec ) )
            return (uint32_t)fs::file_size( filepath, ec );
        if ( auto packed = VFS::FileSize( filepath ) )
            return (uint32_t)*packed;
        return 0;
    }

    const void FileSystem::WriteContentToFile( const std::filesystem::path& filepath, const std::string& content )
    {
        std::ofstream fout( filepath );
        if ( !fout )
        {
            // Was a silent no-op: a prefs/layout/deproj write to an unwritable location just vanished.
            LOG_ERROR( "[FileSystem] Could not write file: {}", filepath.string() );
            return;
        }
        fout << content;
        fout.close();
    }

} // namespace Common::Utils