#include <Common/Utilities/FileSystem.hpp>

#if ( DESERT_PLATFORM_WINDOWS )
#include <Common/Platform/Windows/WindowsFileSystem.hpp>
#endif

#include <Common/Core/Core.hpp>

#include <filesystem>
#include <fstream>

#pragma warning( error : 4834 )

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
        return fs::exists( filepath );
    }

    bool FileSystem::Exists( const std::string& filepath )
    {
        return fs::exists( fs::path( filepath ) );
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
        return WindowsFileSystem::OpenFileDialog( filter );
    }

    std::filesystem::path FileSystem::GetFileDirectory( const std::filesystem::path& filepath )
    {
        return filepath.parent_path();
    }

    std::string FileSystem::GetFileDirectoryString( const std::filesystem::path& filepath )
    {
        return filepath.parent_path().string();
    }

    const std::string FileSystem::ReadFileContent( const std::filesystem::path& filepath )
    {
        std::ifstream in( filepath, std::ios::in | std::ios::binary );
        DESERT_VERIFY( in, "Could not read file! {}", filepath.string().c_str() );

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
        DESERT_VERIFY( file, "Could not open file! {}", filepath.string().c_str() );

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
        return fs::file_size( filepath );
    }

    const void FileSystem::WriteContentToFile( const std::filesystem::path& filepath, const std::string& content )
    {
        std::ofstream fout( filepath );
        fout << content;
        fout.close();
    }

} // namespace Common::Utils