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
// AND IT IS NOW THE ONLY THING THAT MIGRATES. The migrations used to ALSO live in the engine and run on
// every scene load; they are Source/SceneMigration.cpp beside this file now, and Core::kSceneVersion is a
// requirement the loader enforces rather than a target it drags files towards. So this tool is not a
// convenience any more — it is the conversion, and the loader's refusal names it by command line.
//
// It parses into Core::SceneSerialized, the engine's own struct for the current on-disk shape, and writes
// the same tree back out, so the file this produces is the file the engine reads. There is no second
// statement of the format to disagree with the first.
//
// It needs no GPU, no asset manager and no scene graph, because the migrations are pure functions over the
// parsed tree. That is what makes running it over a whole repository safe: a scene whose meshes or
// materials cannot be resolved on this machine still round-trips exactly, because nothing here resolves
// them.
//
//   SceneMigrator <path>...        one or more .desce files, or directories searched recursively
//   SceneMigrator --check <path>...  report what would change and write nothing (exit 1 if any would)

#include "SceneMigration.hpp"

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

        auto parsed = rfl::json::read<Desert::Migration::SceneSerialized>( source );
        if ( !parsed )
        {
            std::cerr << "FAIL   " << path.string() << " — " << parsed.error().what() << "\n";
            ++failed;
            continue;
        }

        const Desert::Migration::SceneMigrationReport report = Desert::Migration::MigrateScene( parsed.value() );
        if ( !report.Changed() )
        {
            std::cout << "ok     " << path.string() << " — already at scene v" << Desert::Migration::kSceneVersion
                      << " / units v" << Desert::Migration::kUnitVersion << "\n";
            continue;
        }

        ++changed;
        std::cout << ( check ? "WOULD  " : "raised " ) << path.string() << " —";
        if ( report.SkyRaised )
            std::cout << " sky v0->v" << Desert::Migration::kSceneVersionSky << " (" << report.Sky.Entities
                      << " entity(ies), " << report.Sky.FieldsCarried << " carried, " << report.Sky.FieldsRejected
                      << " rejected)";
        if ( report.TonemapperRaised )
            std::cout << " scene v" << Desert::Migration::kSceneVersionSky << "->v"
                      << Desert::Migration::kSceneVersionTonemap << " ("
                      << ( report.Tonemap.OperatorPinned ? "tonemapper pinned to Reinhard"
                                                         : "tonemapper NOT pinned — see the warning above" )
                      << ( report.Tonemap.SettingsCreated ? ", settings block created" : "" ) << ")";
        if ( report.CloudNoiseRaised )
        {
            std::cout << " scene v" << Desert::Migration::kSceneVersionTonemap << "->v"
                      << Desert::Migration::kSceneVersionCloudNoise << " (";
            if ( report.CloudNoise.Entities > 0 )
                std::cout << report.CloudNoise.FieldsDropped << " cloud bake setting(s) dropped from "
                          << report.CloudNoise.Entities << " entity(ies)";
            else
                std::cout << "stamp only — no cloud layer carried a bake setting";
            std::cout << ")";
        }
        if ( report.TerrainMaterialRaised )
        {
            std::cout << " scene v" << Desert::Migration::kSceneVersionCloudSet << "->v"
                      << Desert::Migration::kSceneVersionTerrainMaterial << " (";
            if ( report.TerrainMaterial.Entities > 0 )
            {
                // Named, not counted, and for the same reason the loader names them: this step DROPS the
                // values it finds, so the operator running this tool has to be able to see what left.
                std::cout << "inline terrain material removed from " << report.TerrainMaterial.Entities
                          << " entity(ies), dropping " << report.TerrainMaterial.Params << " param(s) and "
                          << report.TerrainMaterial.Textures << " texture(s):";
                for ( const auto& name : report.TerrainMaterial.DroppedNames )
                    std::cout << " " << name;
            }
            else
            {
                std::cout << "stamp only — no terrain entity carried an inline material";
            }
            std::cout << ")";
        }
        if ( report.MaterialPathRaised )
        {
            std::cout << " scene v" << Desert::Migration::kSceneVersionTerrainMaterial << "->v"
                      << Desert::Migration::kSceneVersionMaterialPath << " (";
            if ( report.MaterialPath.Paths > 0 )
                std::cout << report.MaterialPath.Paths << " material path(s) made relative to the assets "
                          << "root in " << report.MaterialPath.Entities << " entity(ies)";
            else
                std::cout << "stamp only - no entity named a material by an absolute path";
            // Named, not counted, for the reason the terrain step names its drops: these are the ones the
            // step could not fix, and the operator has to be able to see which slot to re-point.
            for ( const auto& name : report.MaterialPath.OutsideNames )
                std::cout << "; OUTSIDE the assets root, left absolute: " << name;
            std::cout << ")";
        }
        if ( report.GravityUnitsRaised )
        {
            std::cout << " scene v" << Desert::Migration::kSceneVersionMaterialPath << "->v"
                      << Desert::Migration::kSceneVersionGravityUnits << " (";
            if ( !report.GravityUnits.Found )
                std::cout << "stamp only - the scene states no gravity";
            else if ( report.GravityUnits.Scaled )
                std::cout << "gravity " << report.GravityUnits.Before << " -> " << report.GravityUnits.After
                          << " cm/s2 (metre-era value, x100)";
            else if ( report.GravityUnits.Unrecognised )
                // Named rather than counted, for the same reason the two steps above name what they could
                // not fix: this is the one case the operator has to look at by hand.
                std::cout << "gravity " << report.GravityUnits.Before
                          << " LEFT UNCHANGED - neither Earth in metres nor in centimetres, so it was not "
                             "guessed at";
            else if ( report.GravityUnits.Tidied )
                std::cout << "gravity " << report.GravityUnits.Before << " -> " << report.GravityUnits.After
                          << " cm/s2 (already centimetres; dropped the earlier pass's rounding)";
            else
                std::cout << "gravity already " << report.GravityUnits.After << " cm/s2, unchanged";
            std::cout << ")";
        }
        if ( report.UnitsRaised )
            std::cout << " units v0->v" << Desert::Migration::kUnitVersion << " (" << report.Units.Entities
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
