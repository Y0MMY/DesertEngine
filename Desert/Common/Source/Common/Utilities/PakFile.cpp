#include "PakFile.hpp"

#include <cstring>
#include <fstream>

namespace Common::Utils
{
    namespace
    {
        constexpr char     kMagic[4]   = { 'D', 'P', 'K', '1' };
        constexpr uint64_t kHeaderSize = 4 + sizeof( uint32_t ) + sizeof( uint64_t );

        template <typename T>
        void WritePod( std::ofstream& out, const T& value )
        {
            out.write( reinterpret_cast<const char*>( &value ), sizeof( T ) );
        }

        template <typename T>
        bool ReadPod( std::ifstream& in, T& value )
        {
            in.read( reinterpret_cast<char*>( &value ), sizeof( T ) );
            return static_cast<bool>( in );
        }
    } // namespace

    // ---------------------------------------------------------------- PakWriter

    PakWriter::PakWriter( const std::filesystem::path& pakPath ) : m_Path( pakPath )
    {
        std::error_code ec;
        std::filesystem::create_directories( pakPath.parent_path(), ec );

        std::ofstream out( m_Path, std::ios::binary | std::ios::trunc );
        if ( !out )
            return;

        // Placeholder header; Finalize() rewrites it with the real index offset.
        out.write( kMagic, 4 );
        WritePod<uint32_t>( out, 0 );
        WritePod<uint64_t>( out, 0 );
        m_Cursor = kHeaderSize;
        m_Ok     = static_cast<bool>( out );
    }

    bool PakWriter::IsOpen() const
    {
        return m_Ok;
    }

    bool PakWriter::AddFile( const std::string& key, const std::filesystem::path& sourceFile )
    {
        std::ifstream in( sourceFile, std::ios::binary );
        if ( !in )
            return false;
        std::string data( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
        return AddData( key, data.data(), data.size() );
    }

    bool PakWriter::AddData( const std::string& key, const void* data, size_t size )
    {
        if ( !m_Ok )
            return false;

        std::ofstream out( m_Path, std::ios::binary | std::ios::in | std::ios::out );
        if ( !out )
            return false;
        out.seekp( static_cast<std::streamoff>( m_Cursor ) );
        out.write( static_cast<const char*>( data ), static_cast<std::streamsize>( size ) );
        if ( !out )
            return false;

        m_Entries.push_back( { key, m_Cursor, static_cast<uint64_t>( size ) } );
        m_Cursor += size;
        return true;
    }

    size_t PakWriter::Finalize()
    {
        if ( !m_Ok )
            return 0;

        std::ofstream out( m_Path, std::ios::binary | std::ios::in | std::ios::out );
        if ( !out )
            return 0;

        const uint64_t indexOffset = m_Cursor;
        out.seekp( static_cast<std::streamoff>( indexOffset ) );
        for ( const auto& entry : m_Entries )
        {
            WritePod<uint32_t>( out, static_cast<uint32_t>( entry.Key.size() ) );
            out.write( entry.Key.data(), static_cast<std::streamsize>( entry.Key.size() ) );
            WritePod<uint64_t>( out, entry.Offset );
            WritePod<uint64_t>( out, entry.Size );
        }

        out.seekp( 0 );
        out.write( kMagic, 4 );
        WritePod<uint32_t>( out, static_cast<uint32_t>( m_Entries.size() ) );
        WritePod<uint64_t>( out, indexOffset );
        return out ? m_Entries.size() : 0;
    }

    // ---------------------------------------------------------------- PakReader

    PakReader::PakReader( const std::filesystem::path& pakPath ) : m_Path( pakPath )
    {
        std::ifstream in( m_Path, std::ios::binary );
        if ( !in )
            return;

        char magic[4] = {};
        in.read( magic, 4 );
        if ( !in || std::memcmp( magic, kMagic, 4 ) != 0 )
            return;

        uint32_t entryCount  = 0;
        uint64_t indexOffset = 0;
        if ( !ReadPod( in, entryCount ) || !ReadPod( in, indexOffset ) )
            return;

        in.seekg( static_cast<std::streamoff>( indexOffset ) );
        for ( uint32_t i = 0; i < entryCount; ++i )
        {
            uint32_t pathLen = 0;
            if ( !ReadPod( in, pathLen ) || pathLen == 0 || pathLen > 4096 )
                return;
            std::string key( pathLen, '\0' );
            in.read( key.data(), pathLen );
            Span span;
            if ( !in || !ReadPod( in, span.Offset ) || !ReadPod( in, span.Size ) )
                return;
            m_Index.emplace( std::move( key ), span );
        }
        m_Ok = true;
    }

    bool PakReader::IsOpen() const
    {
        return m_Ok;
    }

    size_t PakReader::EntryCount() const
    {
        return m_Index.size();
    }

    bool PakReader::Contains( const std::string& key ) const
    {
        return m_Index.contains( key );
    }

    std::optional<uint64_t> PakReader::EntrySize( const std::string& key ) const
    {
        const auto it = m_Index.find( key );
        if ( it == m_Index.end() )
            return std::nullopt;
        return it->second.Size;
    }

    std::optional<std::string> PakReader::Read( const std::string& key ) const
    {
        const auto it = m_Index.find( key );
        if ( it == m_Index.end() )
            return std::nullopt;

        // Own stream per read: trivially thread-safe (asset preloading runs on the JobSystem).
        std::ifstream in( m_Path, std::ios::binary );
        if ( !in )
            return std::nullopt;

        std::string data( static_cast<size_t>( it->second.Size ), '\0' );
        in.seekg( static_cast<std::streamoff>( it->second.Offset ) );
        in.read( data.data(), static_cast<std::streamsize>( it->second.Size ) );
        if ( !in )
            return std::nullopt;
        return data;
    }

    std::vector<std::string> PakReader::KeysWithPrefix( const std::string& prefix ) const
    {
        std::vector<std::string> keys;
        for ( const auto& [key, span] : m_Index )
            if ( prefix.empty() || key.rfind( prefix, 0 ) == 0 )
                keys.push_back( key );
        return keys;
    }
} // namespace Common::Utils
