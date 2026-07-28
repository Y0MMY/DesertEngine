#include "AssetReferences.hpp"

#include <algorithm>
#include <cctype>

namespace Desert::Editor
{
    namespace
    {
        bool IsAllDigits( const std::string& s )
        {
            return !s.empty() &&
                   std::all_of( s.begin(), s.end(), []( unsigned char c ) { return std::isdigit( c ); } );
        }

        // Does @p text contain @p token? For all-digit tokens (asset handles) the match must be
        // digit-bounded so a handle isn't found as a substring of a longer number.
        bool ContainsToken( const std::string& text, const std::string& token )
        {
            if ( token.empty() || text.size() < token.size() )
                return false;

            const bool numeric = IsAllDigits( token );
            for ( std::size_t pos = text.find( token ); pos != std::string::npos;
                  pos            = text.find( token, pos + 1 ) )
            {
                if ( !numeric )
                    return true;
                const bool leftOk =
                     pos == 0 || !std::isdigit( static_cast<unsigned char>( text[pos - 1] ) );
                const std::size_t after = pos + token.size();
                const bool        rightOk =
                     after >= text.size() || !std::isdigit( static_cast<unsigned char>( text[after] ) );
                if ( leftOk && rightOk )
                    return true;
            }
            return false;
        }
    } // namespace

    void AssetReferenceIndex::Clear()
    {
        m_Entries.clear();
    }

    void AssetReferenceIndex::Add( Entry entry )
    {
        m_Entries.push_back( std::move( entry ) );
    }

    const std::vector<AssetReferenceIndex::Entry>& AssetReferenceIndex::Entries() const
    {
        return m_Entries;
    }

    const AssetReferenceIndex::Entry* AssetReferenceIndex::Find( const std::string& path ) const
    {
        for ( const auto& e : m_Entries )
            if ( e.Path == path )
                return &e;
        return nullptr;
    }

    std::vector<std::string> AssetReferenceIndex::ReferencersOf( const std::string& path ) const
    {
        std::vector<std::string> out;
        const Entry*             target = Find( path );
        if ( !target )
            return out;

        for ( const auto& e : m_Entries )
        {
            if ( &e == target || e.Text.empty() )
                continue;
            const bool refs = std::any_of( target->Tokens.begin(), target->Tokens.end(),
                                           [&]( const std::string& t ) { return ContainsToken( e.Text, t ); } );
            if ( refs )
                out.push_back( e.Path );
        }
        std::sort( out.begin(), out.end() );
        out.erase( std::unique( out.begin(), out.end() ), out.end() );
        return out;
    }

    bool AssetReferenceIndex::IsReferenced( const std::string& path ) const
    {
        return !ReferencersOf( path ).empty();
    }

    std::vector<std::string> AssetReferenceIndex::Orphans( const std::vector<std::string>& leafExts ) const
    {
        std::vector<std::string> out;
        for ( const auto& e : m_Entries )
        {
            const bool isLeaf = std::find( leafExts.begin(), leafExts.end(), e.Ext ) != leafExts.end();
            if ( isLeaf && !IsReferenced( e.Path ) )
                out.push_back( e.Path );
        }
        std::sort( out.begin(), out.end() );
        return out;
    }
} // namespace Desert::Editor
