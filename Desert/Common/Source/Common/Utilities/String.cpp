#include <Common/Utilities/String.hpp>

namespace Common::Utils
{
    const std::string String::BytesToString( const uint64_t bytes )
    {
        const size_t KB = 1024;
        const size_t MB = KB * 1024;
        const size_t GB = MB * 1024;
        const size_t TB = GB * 1024;

        double      size = static_cast<double>( bytes );
        std::string result;

        if ( bytes >= TB )
        {
            result = std::to_string( size / TB ) + " TB";
        }
        else if ( bytes >= GB )
        {
            result = std::to_string( size / GB ) + " GB";
        }
        else if ( bytes >= MB )
        {
            result = std::to_string( size / MB ) + " MB";
        }
        else if ( bytes >= KB )
        {
            result = std::to_string( size / KB ) + " KB";
        }
        else
        {
            result = std::to_string( bytes ) + " B";
        }

        return result;
    }

    const std::string String::GetPathHash( const Common::Filepath& path )
    {
        return std::to_string( std::hash<std::string>{}( path.string() ) );
    }

} // namespace Common::Utils
