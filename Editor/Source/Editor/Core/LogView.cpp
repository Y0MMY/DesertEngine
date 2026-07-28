#include "LogView.hpp"

#include <algorithm>
#include <cctype>

namespace Desert::Editor
{
    bool LogMatches( const std::string& line, const std::string& query )
    {
        if ( query.empty() )
            return true;
        if ( query.size() > line.size() )
            return false;

        auto lower = []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); };
        auto it    = std::search( line.begin(), line.end(), query.begin(), query.end(),
                                  [&]( char a, char b ) { return lower( a ) == lower( b ); } );
        return it != line.end();
    }

    std::vector<LogRun> CollapseConsecutive( const std::vector<std::pair<std::string, int>>& lines )
    {
        std::vector<LogRun> runs;
        for ( const auto& [text, level] : lines )
        {
            if ( !runs.empty() && runs.back().Text == text && runs.back().Level == level )
                ++runs.back().Count;
            else
                runs.push_back( { text, level, 1 } );
        }
        return runs;
    }
} // namespace Desert::Editor
