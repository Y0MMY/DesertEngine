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
            DESERT_VERIFY( false );
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

            DESERT_VERIFY( in, "Could not read file! {}", filepath.string().c_str() );
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

    int SkipBOM( std::istream& in )
    {
        char test[4] = { 0 };
        in.seekg( 0, std::ios::beg );
        in.read( test, 3 );
        if ( strcmp( test, "\xEF\xBB\xBF" ) == 0 )
        {
            in.seekg( 3, std::ios::beg );
            return 3;
        }
        in.seekg( 0, std::ios::beg );
        return 0;
    }

    // Returns an empty string when failing.
    std::string FileSystem::ReadFileAndSkipBOM( const std::filesystem::path& filepath )
    {
        std::string   result;
        std::ifstream in( filepath, std::ios::in | std::ios::binary );
        if ( in )
        {
            in.seekg( 0, std::ios::end );
            auto      fileSize     = in.tellg();
            const int skippedChars = SkipBOM( in );

            fileSize -= skippedChars - 1;
            result.resize( fileSize );
            in.read( result.data() + 1, fileSize );
            // Add a dummy tab to beginning of file.
            result[0] = '\t';
        }
        in.close();
        return result;
    }

    std::vector<uint8_t> FileSystem::ReadByteFileContent( const std::filesystem::path& filepath )
    {
        std::ifstream file( filepath, std::ios::in | std::ios::binary );
        if ( !file )
        {
            if ( auto packed = VFS::ReadFile( filepath ) )
                return std::vector<uint8_t>( packed->begin(), packed->end() );

            DESERT_VERIFY( file, "Could not open file! {}", filepath.string().c_str() );
            return {};
        }

        file.seekg( 0, std::ios::end );
        std::streamsize fileSize = file.tellg();
        file.seekg( 0, std::ios::beg );

        std::vector<uint8_t> binaryData( fileSize / sizeof( uint8_t ) );
        if ( !file.read( reinterpret_cast<char*>( binaryData.data() ), fileSize ) )
        {
            DESERT_VERIFY( file, "Could not read file! {}", filepath.string().c_str() );
            return {};
        }
        return std::move( binaryData );
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
        fout << content;
        fout.close();
    }

} // namespace Common::Utils