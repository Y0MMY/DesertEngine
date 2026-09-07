// PakTool — CLI for the Desert .dpak archive format (Common::Utils::PakFile, the exact code the
// Runtime mounts). Lets scripts/CI build and inspect content archives without booting the editor.
//
//   PakTool create   <out.dpak> <srcDir> [--prefix P]  pack every file under srcDir (keys relative
//                                                      to it, optionally prefixed "P/...")
//   PakTool list     <archive.dpak>                    print every entry with its size
//   PakTool extract  <archive.dpak> <outDir>           unpack all entries into outDir
//   PakTool manifest <archive.dpak|srcDir> <out.txt>   record what this release hands out
//                                                      [--prefix P]
//   PakTool patch    <base.manifest> <new.dpak> <patch.dpak>
//                                                      patch pak = entries of new that are absent or
//                                                      changed vs the manifest, PLUS the list of keys
//                                                      the manifest had and new does not. Mount it
//                                                      AFTER the base to apply the update.
//
// `patch` REPLACED `diff`, and the difference is not the spelling. `diff` compared two whole archives,
// so publishing an update meant keeping every shipped .dpak for ever (318 MB per version here), and it
// could not express a DELETION at all — its own code counted removals and printed them as "not
// representable in an overlay patch". A manifest of the same tree is 247 KiB, 0.076 % of it, so a
// release keeps the manifest of every version and no old archives; and deletions ride inside the patch
// as a reserved entry (Common/Utilities/PakFile.hpp, kDeletedEntriesKey). `diff` had no caller anywhere
// — not CI, not the Package scripts, not a test — so it is gone rather than kept beside its
// replacement. See Docs/Architecture/P3_CONTENT_MANIFEST.md.

#include <Common/Utilities/ContentManifest.hpp>
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
            // WITH THE REASON. This tool is what a developer runs to find out why a shipped archive
            // will not mount, and "cannot open" answered that question with the question.
            std::fprintf( stderr, "PakTool: cannot open %s: %s\n", pakPath.string().c_str(),
                          reader.OpenError().c_str() );
            return 1;
        }
        for ( const auto& key : reader.KeysWithPrefix( "" ) )
            std::printf( "%10ju  %s\n", (uintmax_t)reader.EntrySize( key ).value_or( 0 ), key.c_str() );
        // Deletions are printed EXPLICITLY because they are invisible everywhere else: the reserved
        // entry is hidden from every content accessor on purpose, so a patch that removes ten files and
        // adds none would otherwise list as an empty archive.
        for ( const auto& key : reader.DeletedKeys() )
            std::printf( "%10s  %s\n", "DELETED", key.c_str() );
        std::printf( "PakTool: %zu entr%s, %zu deletion(s) in %s\n", reader.EntryCount(),
                     reader.EntryCount() == 1 ? "y" : "ies", reader.DeletedKeys().size(),
                     pakPath.string().c_str() );
        return 0;
    }

    int Extract( const fs::path& pakPath, const fs::path& outDir )
    {
        Common::Utils::PakReader reader( pakPath );
        if ( !reader.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot open %s: %s\n", pakPath.string().c_str(),
                          reader.OpenError().c_str() );
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

    // A manifest of whatever the argument is — an archive or the tree an archive is built from. Both
    // spellings must produce the SAME manifest for the same content; that equality is the whole reason
    // a release can record its manifest from either side and later diff the other against it, and it is
    // pinned by ContentManifest.ATreeAndThePakBuiltFromItAgree.
    int Manifest( const fs::path& source, const fs::path& outPath, const std::string& prefix )
    {
        std::error_code                ec;
        Common::Utils::ContentManifest manifest;

        if ( fs::is_directory( source, ec ) )
        {
            auto built = Common::Utils::ContentManifest::FromDirectory( source, prefix );
            if ( !built )
            {
                std::fprintf( stderr, "PakTool: %s\n", built.GetError().c_str() );
                return 1;
            }
            manifest = built.ExtractValue();
        }
        else
        {
            Common::Utils::PakReader reader( source );
            if ( !reader.IsOpen() )
            {
                std::fprintf( stderr, "PakTool: cannot open %s: %s\n", source.string().c_str(),
                              reader.OpenError().c_str() );
                return 1;
            }
            manifest = Common::Utils::ContentManifest::FromPak( reader );
        }

        const std::string text = manifest.Serialize();
        fs::create_directories( outPath.parent_path(), ec );
        std::ofstream out( outPath, std::ios::binary | std::ios::trunc );
        out.write( text.data(), static_cast<std::streamsize>( text.size() ) );
        out.close();
        if ( !out )
        {
            std::fprintf( stderr, "PakTool: failed to write %s\n", outPath.string().c_str() );
            return 1;
        }
        std::printf( "PakTool: manifest of %zu entr%s (%zu bytes) -> %s\n", manifest.Count(),
                     manifest.Count() == 1 ? "y" : "ies", text.size(), outPath.string().c_str() );
        return 0;
    }

    int Patch( const fs::path& baseManifest, const fs::path& newPath, const fs::path& outPath )
    {
        std::ifstream in( baseManifest, std::ios::binary );
        if ( !in )
        {
            std::fprintf( stderr, "PakTool: cannot open %s\n", baseManifest.string().c_str() );
            return 1;
        }
        const std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
        auto              parsed = Common::Utils::ContentManifest::Parse( text );
        if ( !parsed )
        {
            std::fprintf( stderr, "PakTool: %s: %s\n", baseManifest.string().c_str(), parsed.GetError().c_str() );
            return 1;
        }
        const Common::Utils::ContentManifest base = parsed.ExtractValue();

        Common::Utils::PakReader newer( newPath );
        if ( !newer.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot open %s: %s\n", newPath.string().c_str(),
                          newer.OpenError().c_str() );
            return 1;
        }
        const auto diff =
             Common::Utils::CompareManifests( base, Common::Utils::ContentManifest::FromPak( newer ) );

        if ( diff.Empty() )
        {
            std::printf( "PakTool: no differences — no patch written\n" );
            std::error_code ec;
            fs::remove( outPath, ec );
            return 0;
        }

        Common::Utils::PakWriter writer( outPath );
        if ( !writer.IsOpen() )
        {
            std::fprintf( stderr, "PakTool: cannot create %s\n", outPath.string().c_str() );
            return 1;
        }
        for ( const auto* keys : { &diff.Added, &diff.Changed } )
            for ( const auto& key : *keys )
            {
                const auto data = newer.Read( key );
                if ( !data || !writer.AddData( key, data->data(), data->size() ) )
                {
                    std::fprintf( stderr, "PakTool: failed to copy entry %s\n", key.c_str() );
                    return 1;
                }
            }
        if ( !writer.SetDeletedKeys( diff.Removed ) )
        {
            std::fprintf( stderr, "PakTool: a deleted key cannot be recorded (empty, reserved, or "
                                  "containing a line break)\n" );
            return 1;
        }
        if ( writer.Finalize() == 0 )
        {
            std::fprintf( stderr, "PakTool: finalize failed\n" );
            return 1;
        }

        std::printf( "PakTool: patch %s — %zu added, %zu changed, %zu deleted\n", outPath.string().c_str(),
                     diff.Added.size(), diff.Changed.size(), diff.Removed.size() );
        return 0;
    }

    int Usage()
    {
        std::fprintf( stderr, "Usage:\n"
                              "  PakTool create   <out.dpak> <srcDir> [--prefix P]\n"
                              "  PakTool list     <archive.dpak>\n"
                              "  PakTool extract  <archive.dpak> <outDir>\n"
                              "  PakTool manifest <archive.dpak|srcDir> <out.txt> [--prefix P]\n"
                              "  PakTool patch    <base.manifest> <new.dpak> <patch.dpak>\n" );
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
    if ( cmd == "manifest" && argc >= 4 )
    {
        std::string prefix;
        for ( int i = 4; i < argc - 1; ++i )
            if ( std::strcmp( argv[i], "--prefix" ) == 0 )
                prefix = argv[i + 1];
        return Manifest( argv[2], argv[3], prefix );
    }
    if ( cmd == "patch" && argc >= 5 )
        return Patch( argv[2], argv[3], argv[4] );

    return Usage();
}
