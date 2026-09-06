// PrefabMigrator — raises every .deprefab it is pointed at to the current generation and writes it back.
//
// The engine's prefab loader (PrefabAsset::Load via Assets::ParseLoadablePrefab) refuses a prefab that is
// not at Core::kSceneVersion / Core::kUnitVersion and names this tool by command line. This is the thing
// the refusal names. See Source/PrefabMigration.hpp for what the one step does and why it is stamp-only.
//
// THE WRITE IS И2's SHARED ATOMIC PRIMITIVE, NOT A TRUNCATE-IN-PLACE OF OUR OWN: SceneMigrator once
// opened the original with std::ios::trunc and checked only the open, so any failure after that point
// converted "old scene" into "no scene" behind a green report. The fix became ONE primitive
// (Common::Utils::FileSystem::WriteContentToFileAtomic — temp beside the target, stream checked after
// write AND close, then an atomic rename), and this tool uses it rather than writing the dance a second
// time. What this tool adds on top is a gate check BEFORE any byte is written: the migrated text must
// pass THE SAME ParseLoadablePrefab the engine loader applies, so "the tool wrote it" and "the engine
// will load it" cannot drift.
//
//   PrefabMigrator <path>...          one or more .deprefab files, or directories searched recursively
//   PrefabMigrator --check <path>...  report what would change and write nothing (exit 1 if any would)

#include "PrefabMigration.hpp"

#include <Engine/Assets/Prefab/PrefabFormat.hpp>

#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl/json.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kPrefabExtension = ".deprefab";

    void Collect( const std::filesystem::path& root, std::vector<std::filesystem::path>& out )
    {
        std::error_code ec;
        if ( std::filesystem::is_directory( root, ec ) )
        {
            for ( const auto& entry : std::filesystem::recursive_directory_iterator( root, ec ) )
                if ( entry.is_regular_file() && entry.path().extension() == kPrefabExtension )
                    out.push_back( entry.path() );
            return;
        }
        out.push_back( root );
    }

    std::string ReadAll( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // Gate-checks the migrated text against the ENGINE's own loader gate, then hands the bytes to the
    // shared atomic writer. Returns an empty string on success, the failure otherwise — and on every
    // failure the original file is still exactly what it was (the atomic writer's contract).
    std::string WriteBackSafely( const std::filesystem::path& target, const std::string& text )
    {
        if ( auto loadable = Desert::Assets::ParseLoadablePrefab( target.string(), text ); !loadable )
            return "the migrated text does not pass the engine's own gate: " + loadable.GetError();

        if ( !Common::Utils::FileSystem::WriteContentToFileAtomic( target, text ) )
            return "atomic write failed (see the log line above; original untouched)";
        return {};
    }
} // namespace

int main( int argc, char** argv )
{
    bool                               check = false;
    std::vector<std::filesystem::path> roots;

    for ( int i = 1; i < argc; ++i )
    {
        if ( std::strcmp( argv[i], "--check" ) == 0 )
            check = true;
        else
            roots.emplace_back( argv[i] );
    }

    if ( roots.empty() )
    {
        std::cerr << "usage: PrefabMigrator [--check] <prefab.deprefab | directory>...\n";
        return 2;
    }

    std::vector<std::filesystem::path> prefabs;
    for ( const auto& root : roots )
        Collect( root, prefabs );

    if ( prefabs.empty() )
    {
        std::cerr << "PrefabMigrator: no " << kPrefabExtension << " files found\n";
        return 2;
    }

    int changed = 0;
    int failed  = 0;

    for ( const auto& path : prefabs )
    {
        const std::string source = ReadAll( path );
        if ( source.empty() )
        {
            std::cerr << "FAIL   " << path.string() << " — unreadable or empty\n";
            ++failed;
            continue;
        }

        auto parsed = rfl::json::read<Desert::Assets::PrefabData>( source );
        if ( !parsed )
        {
            std::cerr << "FAIL   " << path.string() << " — " << parsed.error().what() << "\n";
            ++failed;
            continue;
        }

        const auto outcome = Desert::Migration::MigratePrefab( parsed.value() );
        if ( !outcome.Error.empty() )
        {
            std::cerr << "FAIL   " << path.string() << " — " << outcome.Error << "\n";
            ++failed;
            continue;
        }
        if ( outcome.AlreadyCurrent )
        {
            std::cout << "ok     " << path.string() << " — already at scene v" << Desert::Core::kSceneVersion
                      << " / units v" << Desert::Core::kUnitVersion << "\n";
            continue;
        }

        std::cout << ( check ? "WOULD  " : "raised " ) << path.string() << " — unversioned (v"
                  << outcome.FoundSceneVersion << "/v" << outcome.FoundUnitVersion << ") stamped to scene v"
                  << Desert::Core::kSceneVersion << " / units v" << Desert::Core::kUnitVersion
                  << " (stamp only; entities untouched)\n";

        if ( check )
        {
            ++changed;
            continue;
        }

        // Serialized through the engine's own stamping writer, so the bytes written are the bytes the
        // saver would produce — one statement of the format, not two.
        if ( const std::string failure =
                  WriteBackSafely( path, Desert::Assets::WritePrefabJson( parsed.value() ) );
             !failure.empty() )
        {
            std::cerr << "FAIL   " << path.string() << " — " << failure << " (original left untouched)\n";
            ++failed;
            continue;
        }

        // Counted only once the bytes are actually on disk, exactly as SceneMigrator does it: a file that
        // printed "raised" and then failed its write is a FAILED file, not a raised one, and a summary
        // that counted it as both would report "1 raised, 1 failed" over a single untouched prefab.
        ++changed;
    }

    std::cout << "PrefabMigrator: " << prefabs.size() << " prefab(s), " << changed
              << ( check ? " would change, " : " raised, " ) << failed << " failed\n";

    if ( failed > 0 )
        return 1;
    return ( check && changed > 0 ) ? 1 : 0;
}
