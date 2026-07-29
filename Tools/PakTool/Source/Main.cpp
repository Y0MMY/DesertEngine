// PakTool — CLI for the Desert .dpak archive format (Common::Utils::PakFile, the exact code the
// Runtime mounts). Lets scripts/CI build and inspect content archives without booting the editor.
//
//   PakTool create  <out.dpak> <srcDir> [--prefix P]   pack every file under srcDir (keys relative
//                                                      to it, optionally prefixed "P/...")
//   PakTool list    <archive.dpak>                     print every entry with its size
//   PakTool extract <archive.dpak> <outDir>            unpack all entries into outDir
//   PakTool diff    <base.dpak> <new.dpak> <patch.dpak> patch pak = entries of new that are absent
//                                                      or changed vs base (per-entry content hash;
//                                                      byte-compare fallback for v1 archives).
//                                                      Mount it AFTER the base to apply the update.

#include <Common/Utilities/PakFile.hpp>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    int Create( const fs::path& out, const fs::path& srcDir, const std::string& prefix )
    {
        std::error_code ec;
        if ( !fs::is_directory( srcDir, ec ) )
        {
            std::fprintf( stderr, "PakTool: not a directory: %s\n", srcDir.string().c_str() );
            return 1;
        }

        Common::Utils::PakWriter writer( out );
        if ( !writer.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot create %s\n", out.string().c_str() );
            return 1;
        }

        size_t    added = 0;
        uintmax_t bytes = 0;
        for ( auto it = fs::recursive_directory_iterator( srcDir, ec );
              it != fs::recursive_directory_iterator(); it.increment( ec ) )
        {
            if ( ec || !it->is_regular_file() )
                continue;
            const fs::path rel = fs::relative( it->path(), srcDir, ec );
            std::string    key = rel.generic_string();
            if ( !prefix.empty() )
                key = prefix + "/" + key;
            if ( !writer.AddFile( key, it->path() ) )
            {
                std::fprintf( stderr, "PakTool: failed to add %s\n", it->path().string().c_str() );
                return 1;
            }
            ++added;
            bytes += fs::file_size( it->path(), ec );
        }

        if ( writer.Finalize() == 0 )
        {
            std::fprintf( stderr, "PakTool: finalize failed (empty archive?)\n" );
            return 1;
        }
        std::printf( "PakTool: %zu file(s), %ju bytes -> %s\n", added, (uintmax_t)bytes,
                     out.string().c_str() );
        return 0;
    }

    int List( const fs::path& pakPath )
    {
        Common::Utils::PakReader reader( pakPath );
        if ( !reader.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot open %s\n", pakPath.string().c_str() );
            return 1;
        }
        for ( const auto& key : reader.KeysWithPrefix( "" ) )
            std::printf( "%10ju  %s\n", (uintmax_t)reader.EntrySize( key ).value_or( 0 ), key.c_str() );
        std::printf( "PakTool: %zu entr%s in %s\n", reader.EntryCount(),
                     reader.EntryCount() == 1 ? "y" : "ies", pakPath.string().c_str() );
        return 0;
    }

    int Extract( const fs::path& pakPath, const fs::path& outDir )
    {
        Common::Utils::PakReader reader( pakPath );
        if ( !reader.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot open %s\n", pakPath.string().c_str() );
            return 1;
        }

        std::error_code ec;
        size_t          written = 0;
        for ( const auto& key : reader.KeysWithPrefix( "" ) )
        {
            const auto data = reader.Read( key );
            if ( !data )
            {
                std::fprintf( stderr, "PakTool: failed to read entry %s\n", key.c_str() );
                return 1;
            }
            const fs::path dst = outDir / fs::path( key );
            fs::create_directories( dst.parent_path(), ec );
            std::ofstream out( dst, std::ios::binary | std::ios::trunc );
            out.write( data->data(), static_cast<std::streamsize>( data->size() ) );
            if ( !out )
            {
                std::fprintf( stderr, "PakTool: failed to write %s\n", dst.string().c_str() );
                return 1;
            }
            ++written;
        }
        std::printf( "PakTool: extracted %zu entr%s -> %s\n", written, written == 1 ? "y" : "ies",
                     outDir.string().c_str() );
        return 0;
    }

    int Diff( const fs::path& basePath, const fs::path& newPath, const fs::path& outPath )
    {
        Common::Utils::PakReader base( basePath );
        Common::Utils::PakReader newer( newPath );
        if ( !base.IsOpen() || !newer.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot open %s\n",
                          ( base.IsOpen() ? newPath : basePath ).string().c_str() );
            return 1;
        }

        Common::Utils::PakWriter writer( outPath );
        if ( !writer.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot create %s\n", outPath.string().c_str() );
            return 1;
        }

        size_t added = 0, changed = 0, removed = 0;
        for ( const auto& key : newer.KeysWithPrefix( "" ) )
        {
            bool same = false;
            if ( base.Contains( key ) )
            {
                const auto oldHash = base.EntryHash( key ).value_or( 0 );
                const auto newHash = newer.EntryHash( key ).value_or( 0 );
                if ( oldHash != 0 && newHash != 0 )
                    same = oldHash == newHash;
                else
                    same = base.Read( key ) == newer.Read( key ); // v1 archive: no hashes -> bytes
            }
            if ( same )
                continue;

            const auto data = newer.Read( key );
            if ( !data || !writer.AddData( key, data->data(), data->size() ) )
            {
                std::fprintf( stderr, "PakTool: failed to copy entry %s\n", key.c_str() );
                return 1;
            }
            base.Contains( key ) ? ++changed : ++added;
        }

        // Deletions are NOT representable in an overlay patch (a later mount can only override).
        // Report them so the packager knows a full base re-ship is needed to actually drop files.
        for ( const auto& key : base.KeysWithPrefix( "" ) )
            if ( !newer.Contains( key ) )
                ++removed;

        if ( added + changed == 0 )
        {
            std::printf( "PakTool: no differences — no patch written\n" );
            std::error_code ec;
            fs::remove( outPath, ec );
            return 0;
        }
        if ( writer.Finalize() == 0 )
        {
            std::fprintf( stderr, "PakTool: finalize failed\n" );
            return 1;
        }

        std::string removedNote;
        if ( removed )
            removedNote =
                 " (" + std::to_string( removed ) + " deletion(s) not representable in an overlay patch)";
        std::printf( "PakTool: patch %s — %zu added, %zu changed%s\n", outPath.string().c_str(), added,
                     changed, removedNote.c_str() );
        return 0;
    }

    int Usage()
    {
        std::fprintf( stderr, "Usage:\n"
                              "  PakTool create  <out.dpak> <srcDir> [--prefix P]\n"
                              "  PakTool list    <archive.dpak>\n"
                              "  PakTool extract <archive.dpak> <outDir>\n"
                              "  PakTool diff    <base.dpak> <new.dpak> <patch.dpak>\n" );
        return 2;
    }
} // namespace

int main( int argc, char** argv )
{
    if ( argc < 3 )
        return Usage();

    const std::string cmd = argv[1];
    if ( cmd == "create" && argc >= 4 )
    {
        std::string prefix;
        for ( int i = 4; i < argc - 1; ++i )
            if ( std::strcmp( argv[i], "--prefix" ) == 0 )
                prefix = argv[i + 1];
        return Create( argv[2], argv[3], prefix );
    }
    if ( cmd == "list" )
        return List( argv[2] );
    if ( cmd == "extract" && argc >= 4 )
        return Extract( argv[2], argv[3] );
    if ( cmd == "diff" && argc >= 5 )
        return Diff( argv[2], argv[3], argv[4] );

    return Usage();
}
