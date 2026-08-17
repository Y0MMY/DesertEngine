// SceneMigrator — raises every .desce it is pointed at to the current scene generation and writes it back.
//
// WHY THIS EXISTS. The engine has always migrated old scenes on LOAD, and never once written the result
// down. Nothing in the repository carried a UnitVersion, so every load of every scene re-ran the
// metres-to-centimetres migration, the files stayed permanently authored in metres, and a scene authored
// correctly in world units was silently multiplied by a hundred the first time anyone opened it. The
// contract's migration clause (DEV_CONTRACT §4.3/§4.5) says data migrates once and is written back in the
// new form, and that "the scenes in the repository are converted by the same task". This is the thing that
// converts them.
//
// It is deliberately the ENGINE'S OWN CODE PATH and not a text substitution: it parses into
// Core::SceneSerialized, calls Core::MigrateScene() — the same function the loader calls on the same
// struct — and writes the same tree back out. There is no second implementation of the migration to
// disagree with the first.
//
// It needs no GPU, no asset manager and no scene graph, because the migrations are pure functions over the
// parsed tree. That is what makes running it over a whole repository safe: a scene whose meshes or
// materials cannot be resolved on this machine still round-trips exactly, because nothing here resolves
// them.
//
//   SceneMigrator <path>...        one or more .desce files, or directories searched recursively
//   SceneMigrator --check <path>...  report what would change and write nothing (exit 1 if any would)

#include <Engine/Core/Serialize/SceneMigration.hpp>

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
    constexpr const char* kSceneExtension = ".desce";

    void Collect( const std::filesystem::path& root, std::vector<std::filesystem::path>& out )
    {
        std::error_code ec;
        if ( std::filesystem::is_directory( root, ec ) )
        {
            for ( const auto& entry : std::filesystem::recursive_directory_iterator( root, ec ) )
                if ( entry.is_regular_file() && entry.path().extension() == kSceneExtension )
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
        std::cerr << "usage: SceneMigrator [--check] <scene.desce | directory>...\n";
        return 2;
    }

    std::vector<std::filesystem::path> scenes;
    for ( const auto& root : roots )
        Collect( root, scenes );

    if ( scenes.empty() )
    {
        std::cerr << "SceneMigrator: no " << kSceneExtension << " files found\n";
        return 2;
    }

    int changed = 0;
    int failed  = 0;

    for ( const auto& path : scenes )
    {
        const std::string source = ReadAll( path );
        if ( source.empty() )
        {
            std::cerr << "FAIL   " << path.string() << " — unreadable or empty\n";
            ++failed;
            continue;
        }

        auto parsed = rfl::json::read<Desert::Core::SceneSerialized>( source );
        if ( !parsed )
        {
            std::cerr << "FAIL   " << path.string() << " — " << parsed.error().what() << "\n";
            ++failed;
            continue;
        }

        const Desert::Core::SceneMigrationReport report = Desert::Core::MigrateScene( parsed.value() );
        if ( !report.Changed() )
        {
            std::cout << "ok     " << path.string() << " — already at sky v" << Desert::Core::kSceneVersion
                      << " / units v" << Desert::Core::kUnitVersion << "\n";
            continue;
        }

        ++changed;
        std::cout << ( check ? "WOULD  " : "raised " ) << path.string() << " —";
        if ( report.SkyRaised )
            std::cout << " sky v0->v" << Desert::Core::kSceneVersion << " (" << report.Sky.Entities
                      << " entity(ies), " << report.Sky.FieldsCarried << " carried, " << report.Sky.FieldsRejected
                      << " rejected)";
        if ( report.UnitsRaised )
            std::cout << " units v0->v" << Desert::Core::kUnitVersion << " (" << report.Units.Entities
                      << " entity(ies), " << report.Units.Values << " value(s) x100, " << report.Units.Rejected
                      << " rejected)";
        std::cout << "\n";

        if ( check )
            continue;

        std::ofstream out( path, std::ios::binary | std::ios::trunc );
        if ( !out )
        {
            std::cerr << "FAIL   " << path.string() << " — could not open for writing\n";
            ++failed;
            continue;
        }
        out << rfl::json::write( parsed.value() );
    }

    std::cout << "SceneMigrator: " << scenes.size() << " scene(s), " << changed
              << ( check ? " would change, " : " raised, " ) << failed << " failed\n";

    if ( failed > 0 )
        return 1;
    return ( check && changed > 0 ) ? 1 : 0;
}
