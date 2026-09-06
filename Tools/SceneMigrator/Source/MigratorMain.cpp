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

#include "MigratorMain.hpp"
#include "SceneMigration.hpp"

#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl/json.hpp>

#include <filesystem>
#include <fstream>
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

namespace Desert::Migration
{
    int RunSceneMigrator( const std::vector<std::string>& args, std::ostream& out, std::ostream& err )
    {
        bool                               check = false;
        std::vector<std::filesystem::path> roots;

        for ( const std::string& arg : args )
        {
            if ( arg == "--check" )
                check = true;
            else
                roots.emplace_back( arg );
        }

        if ( roots.empty() )
        {
            err << "usage: SceneMigrator [--check] <scene.desce | directory>...\n";
            return 2;
        }

        std::vector<std::filesystem::path> scenes;
        for ( const auto& root : roots )
            Collect( root, scenes );

        if ( scenes.empty() )
        {
            err << "SceneMigrator: no " << kSceneExtension << " files found\n";
            return 2;
        }

        int changed = 0;
        int failed  = 0;

        for ( const auto& path : scenes )
        {
            const std::string source = ReadAll( path );
            if ( source.empty() )
            {
                err << "FAIL   " << path.string() << " — unreadable or empty\n";
                ++failed;
                continue;
            }

            auto parsed = rfl::json::read<Desert::Migration::SceneSerialized>( source );
            if ( !parsed )
            {
                err << "FAIL   " << path.string() << " — " << parsed.error().what() << "\n";
                ++failed;
                continue;
            }

            const Desert::Migration::SceneMigrationReport report =
                 Desert::Migration::MigrateScene( parsed.value() );
            if ( !report.Changed() )
            {
                out << "ok     " << path.string() << " — already at scene v" << Desert::Migration::kSceneVersion
                    << " / units v" << Desert::Migration::kUnitVersion << "\n";
                continue;
            }

            out << ( check ? "WOULD  " : "raised " ) << path.string() << " —";
            if ( report.SkyRaised )
                out << " sky v0->v" << Desert::Migration::kSceneVersionSky << " (" << report.Sky.Entities
                    << " entity(ies), " << report.Sky.FieldsCarried << " carried, " << report.Sky.FieldsRejected
                    << " rejected)";
            if ( report.TonemapperRaised )
                out << " scene v" << Desert::Migration::kSceneVersionSky << "->v"
                    << Desert::Migration::kSceneVersionTonemap << " ("
                    << ( report.Tonemap.OperatorPinned ? "tonemapper pinned to Reinhard"
                                                       : "tonemapper NOT pinned — see the warning above" )
                    << ( report.Tonemap.SettingsCreated ? ", settings block created" : "" ) << ")";
            if ( report.CloudNoiseRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionTonemap << "->v"
                    << Desert::Migration::kSceneVersionCloudNoise << " (";
                if ( report.CloudNoise.Entities > 0 )
                    out << report.CloudNoise.FieldsDropped << " cloud bake setting(s) dropped from "
                        << report.CloudNoise.Entities << " entity(ies)";
                else
                    out << "stamp only — no cloud layer carried a bake setting";
                out << ")";
            }
            // The three cloud steps below went unreported until 2026-09-06: Changed() was true, the file
            // was rewritten, and the report line skipped straight from v3 to v6 — a silent migration,
            // which §4.7 forbids ("log which scene, from which version to which, and how many fields
            // moved"). Their counters existed all along; only the printing was missing.
            if ( report.CloudSpeciesRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionCloudNoise << "->v"
                    << Desert::Migration::kSceneVersionCloudSpecies << " (";
                if ( report.CloudSpecies.Entities > 0 )
                    out << report.CloudSpecies.FieldsDropped << " authored shell field(s) dropped and "
                        << report.CloudSpecies.SpeciesSet << " scalar type(s) turned into a species on "
                        << report.CloudSpecies.Entities << " entity(ies)";
                else
                    out << "stamp only — no cloud layer carried the scalar-type shape";
                out << ")";
            }
            if ( report.CloudTypeRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionCloudSpecies << "->v"
                    << Desert::Migration::kSceneVersionCloudType << " (";
                if ( report.CloudType.Entities > 0 )
                {
                    out << report.CloudType.TypesSet << " species enumerator(s) became a .decloudtype path on "
                        << report.CloudType.Entities << " entity(ies)";
                    // Named loud, not folded into the count: these two are the cases the operator has to
                    // act on — a layer that lost its noise volume renders a different sky until re-pointed.
                    if ( report.CloudType.VolumesLost > 0 )
                        out << "; " << report.CloudType.VolumesLost
                            << " layer noise volume(s) DROPPED — re-point them on the cloud type";
                    if ( report.CloudType.FieldsBroken > 0 )
                        out << "; " << report.CloudType.FieldsBroken
                            << " unreadable species value(s) left at the default";
                }
                else
                {
                    out << "stamp only — no cloud layer named a species";
                }
                out << ")";
            }
            if ( report.CloudSetRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionCloudType << "->v"
                    << Desert::Migration::kSceneVersionCloudSet << " (";
                if ( report.CloudSet.Entities > 0 )
                    out << report.CloudSet.SlotsCarried << " cloud type(s) moved into slot 1 of the set on "
                        << report.CloudSet.Entities << " entity(ies), " << report.CloudSet.SlotsEmpty
                        << " of them the empty handle";
                else
                    out << "stamp only — no cloud layer carried a single-type key";
                out << ")";
            }
            if ( report.TerrainMaterialRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionCloudSet << "->v"
                    << Desert::Migration::kSceneVersionTerrainMaterial << " (";
                if ( report.TerrainMaterial.Entities > 0 )
                {
                    // Named, not counted, and for the same reason the loader names them: this step DROPS the
                    // values it finds, so the operator running this tool has to be able to see what left.
                    out << "inline terrain material removed from " << report.TerrainMaterial.Entities
                        << " entity(ies), dropping " << report.TerrainMaterial.Params << " param(s) and "
                        << report.TerrainMaterial.Textures << " texture(s):";
                    for ( const auto& name : report.TerrainMaterial.DroppedNames )
                        out << " " << name;
                }
                else
                {
                    out << "stamp only — no terrain entity carried an inline material";
                }
                out << ")";
            }
            if ( report.MaterialPathRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionTerrainMaterial << "->v"
                    << Desert::Migration::kSceneVersionMaterialPath << " (";
                if ( report.MaterialPath.Paths > 0 )
                    out << report.MaterialPath.Paths << " material path(s) made relative to the assets "
                        << "root in " << report.MaterialPath.Entities << " entity(ies)";
                else
                    out << "stamp only - no entity named a material by an absolute path";
                // Named, not counted, for the reason the terrain step names its drops: these are the ones the
                // step could not fix, and the operator has to be able to see which slot to re-point.
                for ( const auto& name : report.MaterialPath.OutsideNames )
                    out << "; OUTSIDE the assets root, left absolute: " << name;
                out << ")";
            }
            if ( report.GravityUnitsRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionMaterialPath << "->v"
                    << Desert::Migration::kSceneVersionGravityUnits << " (";
                if ( !report.GravityUnits.Found )
                    out << "stamp only - the scene states no gravity";
                else if ( report.GravityUnits.Scaled )
                    out << "gravity " << report.GravityUnits.Before << " -> " << report.GravityUnits.After
                        << " cm/s2 (metre-era value, x100)";
                else if ( report.GravityUnits.Unrecognised )
                    // Named rather than counted, for the same reason the two steps above name what they could
                    // not fix: this is the one case the operator has to look at by hand.
                    out << "gravity " << report.GravityUnits.Before
                        << " LEFT UNCHANGED - neither Earth in metres nor in centimetres, so it was not "
                           "guessed at";
                else if ( report.GravityUnits.Tidied )
                    out << "gravity " << report.GravityUnits.Before << " -> " << report.GravityUnits.After
                        << " cm/s2 (already centimetres; dropped the earlier pass's rounding)";
                else
                    out << "gravity already " << report.GravityUnits.After << " cm/s2, unchanged";
                out << ")";
            }
            if ( report.UIVisibilityRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionGravityUnits << "->v"
                    << Desert::Migration::kSceneVersionUIVisibility << " (";
                if ( report.UIVisibility.Entities > 0 )
                    out << report.UIVisibility.FlagsDropped << " interaction flag(s) folded into Hit Test on "
                        << report.UIVisibility.Entities << " element(s), " << report.UIVisibility.HitTestSet
                        << " of which stopped being the default";
                else
                    out << "stamp only - no UI element stated an interaction flag";
                // Named, not counted, for the reason the two steps above name what they could not carry: an
                // element whose flag was unreadable keeps the default, and the operator has to see which one.
                for ( const auto& name : report.UIVisibility.BrokenNames )
                    out << "; NOT a boolean, left at the default Hit Test: " << name;
                out << ")";
            }
            if ( report.SSRUnitsRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionUIVisibility << "->v"
                    << Desert::Migration::kSceneVersionSSRUnits << " (";
                if ( report.SSRUnits.Scaled )
                    out << "SSR max distance " << report.SSRUnits.Before << " -> " << report.SSRUnits.After
                        << " cm (metre-era slider value, x100)";
                else
                    out << "stamp only - the scene states no SSR max distance";
                out << ")";
            }
            if ( report.CloudMaterialRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionSSRUnits << "->v"
                    << Desert::Migration::kSceneVersionCloudMaterial << " (";
                if ( report.CloudMaterial.Entities > 0 )
                    out << report.CloudMaterial.ValuesMoved << " value(s) and " << report.CloudMaterial.AssetsMoved
                        << " asset slot(s) moved into " << report.CloudMaterial.Materials.size()
                        << " cloud material(s), " << report.CloudMaterial.Defaulted
                        << " left at the schema default";
                else
                    out << "stamp only - no cloud layer stated a look field";
                // Named, not counted, like every step above that can refuse a value: a rejected number is
                // an authored one that will now read as the default, and the operator has to see which.
                for ( const auto& name : report.CloudMaterial.RejectedNames )
                    out << "; NOT carried, schema default stands: " << name;
                out << ")";
            }
            if ( report.UnitsRaised )
                out << " units v0->v" << Desert::Migration::kUnitVersion << " (" << report.Units.Entities
                    << " entity(ies), " << report.Units.Values << " value(s) x100, " << report.Units.Rejected
                    << " rejected)";
            out << "\n";

            if ( check )
            {
                ++changed;
                continue;
            }

            // The material files the v11 -> v12 step produced, written FIRST and atomically, like the
            // scene below: a scene that names a material which does not exist is worse than a scene not
            // yet migrated, so if a material cannot be written the scene is not either.
            bool materialsFailed = false;
            for ( const auto& mat : report.CloudMaterial.Materials )
            {
                // Under the same assets root MigrateScene measured against (its default argument): the
                // relative path inside the scene and the file on disk must agree about one root or the
                // scene names a material that is not where it says.
                const std::filesystem::path matPath = Common::Constants::Path::ASSETS_PATH / mat.RelativePath;
                std::error_code             ec;
                std::filesystem::create_directories( matPath.parent_path(), ec );
                if ( !Common::Utils::FileSystem::WriteContentToFileAtomic( matPath, mat.Json ) )
                {
                    err << "FAIL   " << matPath.string() << " — the cloud material could not be written; "
                        << path.string() << " is left at its old version\n";
                    materialsFailed = true;
                    break;
                }
                out << "        wrote " << mat.RelativePath << "\n";
            }
            if ( materialsFailed )
            {
                ++failed;
                continue;
            }

            // Write-then-rename, and the verdict comes from the WRITE rather than from the open. This
            // used to open the scene ITSELF with trunc and check only that the open worked — so by the
            // time a full disk, dropped permissions or a killed process stopped the write, the scene
            // was already zero bytes, and the tool still printed "raised" and exited 0 over the wreck.
            // The atomic primitive never opens the original at all; a failure at any step leaves it
            // byte-identical, and is a failed FILE here: counted, named, fatal to the exit code like
            // every FAIL above. The "raised" line above then describes work that was NOT kept, which is
            // why this line says so explicitly.
            if ( !Common::Utils::FileSystem::WriteContentToFileAtomic( path, rfl::json::write( parsed.value() ) ) )
            {
                err << "FAIL   " << path.string() << " — the raise could not be written; the original file "
                    << "is untouched\n";
                ++failed;
                continue;
            }
            ++changed;
        }

        out << "SceneMigrator: " << scenes.size() << " scene(s), " << changed
            << ( check ? " would change, " : " raised, " ) << failed << " failed\n";

        if ( failed > 0 )
            return 1;
        return ( check && changed > 0 ) ? 1 : 0;
    }
} // namespace Desert::Migration
