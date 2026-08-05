#include "UIDataStore.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace Desert::UI
{
    UIDataStore& UIDataStore::Get()
    {
        static UIDataStore store;
        return store;
    }

    void UIDataStore::Set( const std::string& key, Value value )
    {
        if ( !key.empty() )
            m_Values[key] = std::move( value );
    }

    void UIDataStore::Erase( const std::string& key )
    {
        m_Values.erase( key );
    }

    void UIDataStore::Clear()
    {
        m_Values.clear();
    }

    bool UIDataStore::Has( const std::string& key ) const
    {
        return m_Values.find( key ) != m_Values.end();
    }

    std::optional<double> UIDataStore::Number( const std::string& key ) const
    {
        const auto it = m_Values.find( key );
        if ( it == m_Values.end() )
            return std::nullopt;
        if ( const double* d = std::get_if<double>( &it->second ) )
            return *d;
        if ( const bool* b = std::get_if<bool>( &it->second ) )
            return *b ? 1.0 : 0.0;
        if ( const std::string* s = std::get_if<std::string>( &it->second ) )
        {
            try // a numeric string is a number the author clearly meant
            {
                return std::stod( *s );
            }
            catch ( ... )
            {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<bool> UIDataStore::Bool( const std::string& key ) const
    {
        const auto it = m_Values.find( key );
        if ( it == m_Values.end() )
            return std::nullopt;
        if ( const bool* b = std::get_if<bool>( &it->second ) )
            return *b;
        if ( const double* d = std::get_if<double>( &it->second ) )
            return *d != 0.0;
        if ( const std::string* s = std::get_if<std::string>( &it->second ) )
        {
            std::string lower = *s;
            std::transform( lower.begin(), lower.end(), lower.begin(),
                            []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            if ( lower == "true" || lower == "1" || lower == "yes" )
                return true;
            if ( lower == "false" || lower == "0" || lower == "no" )
                return false;
        }
        return std::nullopt;
    }

    std::optional<std::string> UIDataStore::Text( const std::string& key ) const
    {
        const auto it = m_Values.find( key );
        if ( it == m_Values.end() )
            return std::nullopt;
        if ( const std::string* s = std::get_if<std::string>( &it->second ) )
            return *s;
        if ( const bool* b = std::get_if<bool>( &it->second ) )
            return std::string( *b ? "true" : "false" );
        if ( const double* d = std::get_if<double>( &it->second ) )
        {
            // Whole numbers read as "42", not "42.000000" — a score or an HP value is the common case.
            char buf[64];
            if ( *d == static_cast<double>( static_cast<long long>( *d ) ) )
                std::snprintf( buf, sizeof( buf ), "%lld", static_cast<long long>( *d ) );
            else
                std::snprintf( buf, sizeof( buf ), "%g", *d );
            return std::string( buf );
        }
        return std::nullopt;
    }

    std::optional<glm::vec3> UIDataStore::Color( const std::string& key ) const
    {
        const auto it = m_Values.find( key );
        if ( it == m_Values.end() )
            return std::nullopt;
        if ( const glm::vec3* c = std::get_if<glm::vec3>( &it->second ) )
            return *c;
        return std::nullopt;
    }
} // namespace Desert::UI

namespace Desert::UI
{
    UIMessageQueue& UIMessageQueue::Get()
    {
        static UIMessageQueue queue;
        return queue;
    }

    void UIMessageQueue::Push( std::string message )
    {
        if ( message.empty() )
            return;
        // A UI can only produce so many events per frame; the cap is purely a runaway guard (a script
        // that answers a message by raising another would otherwise grow this without bound).
        constexpr size_t kMax = 256;
        if ( m_Messages.size() < kMax )
            m_Messages.push_back( std::move( message ) );
    }

    std::vector<std::string> UIMessageQueue::Drain()
    {
        std::vector<std::string> out;
        out.swap( m_Messages );
        return out;
    }

    void UIMessageQueue::Clear()
    {
        m_Messages.clear();
    }
} // namespace Desert::UI
