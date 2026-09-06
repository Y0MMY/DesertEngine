#include "Files.hpp"

#include <fstream>
#include <sstream>

namespace Hub
{
    namespace fs = std::filesystem;

    Common::ResultStr<std::string> ReadTextFile( const fs::path& path )
    {
        std::ifstream in( path, std::ios::binary );
        if ( !in )
            return Common::MakeFormattedError<std::string>( "Could not open {}", path.string() );

        std::ostringstream buffer;
        buffer << in.rdbuf();
        if ( in.bad() )
            return Common::MakeFormattedError<std::string>( "Could not read {}", path.string() );
        return Common::MakeSuccess( buffer.str() );
    }

    Common::BoolResultStr WriteTextFile( const fs::path& path, const std::string& content )
    {
        fs::path temp = path;
        temp += ".tmp";

        std::ofstream out( temp, std::ios::binary | std::ios::trunc );
        if ( !out )
            return Common::MakeFormattedError( "could not open the temporary file {} ({} is unchanged)",
                                               temp.string(), path.string() );

        out << content;
        out.close(); // flushes; a buffered failure (disk full, volume gone) may only surface here
        if ( !out )
        {
            std::error_code removeEc;
            fs::remove( temp, removeEc );
            return Common::MakeFormattedError( "could not write {} bytes to {} ({} is unchanged)", content.size(),
                                               temp.string(), path.string() );
        }

        std::error_code renameEc;
        fs::rename( temp, path, renameEc ); // POSIX rename(2) / MoveFileExW: replaces atomically
        if ( renameEc )
        {
            std::error_code removeEc;
            fs::remove( temp, removeEc );
            return Common::MakeFormattedError( "could not rename {} over {}: {} ({} is unchanged)", temp.string(),
                                               path.string(), renameEc.message(), path.string() );
        }
        return Common::MakeSuccess( true );
    }
} // namespace Hub
