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
// AND IT RAISES `.demat` FILES TOO, since O-4. The cloud LOOK has lived in a material rather than in the
// scene since v12, so a tool that migrated only scenes would leave every cloud material behind — carrying,
// in that case, a layout slot the shader no longer declares. A `.demat` has no version field, so those
// steps are content-detected and idempotent rather than version-gated; see
// MigrateCloudMaterialLayoutInputs and MigrateCloudMaterialAlbedoToColour.
//
// TWO STEPS NOW, AND NEITHER GATES THE OTHER. The second raises a scalar `ScatteringAlbedo` to a neutral
// colour, which is DATA LOSS if it is skipped rather than a missing feature: a scalar stored as
// (x, 0, 0, 0) and read as a colour is a medium that scatters red and absorbs green and blue outright.
// A material can need it without ever having had a layout binding, so both reports are consulted before
// the file is called clean.
//
//   SceneMigrator <path>...        .desce and .demat files, or directories searched recursively
//   SceneMigrator --check <path>...  report what would change and write nothing (exit 1 if any would)

#include "MigratorMain.hpp"
#include "SceneMigration.hpp"
#include "SettingsCanonical.hpp"

#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <rflcpp/rfl/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kSceneExtension = ".desce";

    // MATERIALS ARE COLLECTED TOO, since O-4. A `.demat` has no version field to gate on, so the
    // material step is content-detected (see MigrateCloudMaterialLayoutInputs) — but it still has to be
    // REACHED, and the cloud look has lived in `.demat` files rather than in scenes since v12. A tool
    // that migrated only the scenes would leave every cloud material behind with a slot the shader no
    // longer declares.
    constexpr const char* kMaterialExtension = ".demat";

    void Collect( const std::filesystem::path& root, std::vector<std::filesystem::path>& scenes,
                  std::vector<std::filesystem::path>& materials )
    {
        std::error_code ec;
        if ( std::filesystem::is_directory( root, ec ) )
        {
            for ( const auto& entry : std::filesystem::recursive_directory_iterator( root, ec ) )
            {
                if ( !entry.is_regular_file() )
                    continue;
                if ( entry.path().extension() == kSceneExtension )
                    scenes.push_back( entry.path() );
                else if ( entry.path().extension() == kMaterialExtension )
                    materials.push_back( entry.path() );
            }
            return;
        }

        if ( root.extension() == kMaterialExtension )
            materials.push_back( root );
        else
            scenes.push_back( root );
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
    std::filesystem::path SceneOutputRoot( const std::filesystem::path& scenePath )
    {
        if ( const auto root = Common::Constants::Path::RootForContentPath(
                  Common::Constants::Path::ContentDir::Scene, scenePath ) )
            return *root;

        // Not under a `Scenes/` folder, so the census has nothing to say: the scene's own directory is
        // the root. `parent_path()` is empty for a bare `x.desce`, and an empty root would resolve the
        // material against the working directory by a different route — the very thing being fixed — so
        // it is spelled as the current directory explicitly.
        const std::filesystem::path directory = scenePath.parent_path();
        return directory.empty() ? std::filesystem::path( "." ) : directory;
    }

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
            err << "usage: SceneMigrator [--check] <scene.desce | material.demat | directory>...\n";
            return 2;
        }

        std::vector<std::filesystem::path> scenes;
        std::vector<std::filesystem::path> materials;
        for ( const auto& root : roots )
            Collect( root, scenes, materials );

        if ( scenes.empty() && materials.empty() )
        {
            err << "SceneMigrator: no " << kSceneExtension << " or " << kMaterialExtension << " files found\n";
            return 2;
        }

        int changed = 0;
        int failed  = 0;

        // Cloud material FILE -> the scene that produced it, for the collision check below. Keyed on the
        // resolved path and not on the relative name any more: the root is per-scene now, so two scenes
        // sharing a SceneName under two different assets roots produce two different files and are not in
        // conflict at all, while the case the guard exists for — one file, two scenes — is exactly a
        // repeated key here.
        std::map<std::string, std::string> writtenMaterials;

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

            // ONE root for this scene, and it is the scene's own (see SceneOutputRoot). It is handed to
            // the migration as well as used for the write below, so the root the v7 -> v8 step measures
            // material paths against and the root the v11 -> v12 material is written under are the same
            // root by construction — the two used to be one global read twice, which is how a path
            // written into the scene could name a place the file was not.
            const std::filesystem::path assetsRoot = SceneOutputRoot( path );

            const Desert::Migration::SceneMigrationReport report =
                 Desert::Migration::MigrateScene( parsed.value(), assetsRoot );

            // CANONICALISATION IS THE TOOL'S, NOT A SCHEMA STEP'S, and the split is structural rather
            // than tidiness. Every function in SceneMigration.hpp is pure over the parsed tree, which is
            // what lets sixteen suites compile that one translation unit and test a step each with no
            // engine linked; this needs the engine's reflection table, so it lives beside main where the
            // table is already paid for. It runs on the same gate as the retirement step and AFTER it —
            // canonical means "the fields the table describes", and a retired key is by definition not
            // one of them, so running it first would drop the key with nothing left to report.
            const Desert::Migration::SettingsCanonicalisationReport canonical =
                 report.RetiredKeysRaised ? Desert::Migration::CanonicaliseSettings( parsed.value().Settings )
                                          : Desert::Migration::SettingsCanonicalisationReport{};

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
                {
                    out << report.CloudMaterial.ValuesMoved << " value(s) and " << report.CloudMaterial.AssetsMoved
                        << " asset slot(s) moved into " << report.CloudMaterial.Materials.size()
                        << " bespoke cloud material(s), " << report.CloudMaterial.DefaultsAssigned
                        << " layer(s) pointed at the shared "
                        << Desert::Migration::kDefaultCloudMaterialRelativePath << " (D-37), "
                        << report.CloudMaterial.Defaulted << " field(s) left at the schema default";
                }
                else
                    out << "stamp only - no VolumetricCloud payload in this scene";
                // Named, not counted, like every step above that can refuse a value: a rejected number is
                // an authored one that will now read as the default, and the operator has to see which.
                for ( const auto& name : report.CloudMaterial.RejectedNames )
                    out << "; NOT carried, schema default stands: " << name;
                out << ")";
            }
            if ( report.DebugViewRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionCloudMaterial << "->v"
                    << Desert::Migration::kSceneVersionDebugView << " (";
                if ( report.DebugView.KeysRemoved > 0 )
                {
                    // Named with their values, not counted: these were AUTHORED flags, and the operator has
                    // to see that (say) the collider wireframes stopped because the file stopped deciding
                    // them - not because something broke.
                    out << report.DebugView.KeysRemoved << " viewport debug key(s) removed - the view owns "
                        << "them now (editor Show flags / View Mode):";
                    for ( const auto& name : report.DebugView.RemovedNames )
                        out << " " << name;
                }
                else
                {
                    out << "stamp only - the scene stated no viewport debug flag";
                }
                out << ")";
            }
            if ( report.ScriptRootRaised )
            {
                // v15, not the previous PRINTED step (v13): 14 and 15 are rows of kRetiredKeys rather
                // than steps of their own, so the last line above this one names 13 and a file arriving
                // here is at 15. Printing the previous printed number would report a transition no file
                // made — the same wrong-transition trap the retired-keys line below documents.
                out << " scene v" << Desert::Migration::kSceneVersionMachineQuality << "->v"
                    << Desert::Migration::kSceneVersionScriptRoot << " (";
                if ( report.ScriptRoot.Slots > 0 )
                    out << report.ScriptRoot.Slots << " script reference(s) root-tagged on "
                        << report.ScriptRoot.Entities << " entity(ies), " << report.ScriptRoot.Empty
                        << " of them an empty slot";
                else if ( report.ScriptRoot.UnrootedNames.empty() )
                    out << "stamp only - no entity named a script";
                else
                    out << "no reference could be root-tagged";
                // Named, not counted, like every step above that can refuse a value: a reference the
                // census could not place still does not resolve in a packaged game, and the operator has
                // to see which entity to re-point.
                for ( const auto& name : report.ScriptRoot.UnrootedNames )
                    out << "; NOT under a Scripts/ folder, carried over untagged: " << name;
                out << ")";
            }
            if ( report.ServiceAssetRootRaised )
            {
                out << " scene v" << Desert::Migration::kSceneVersionScriptRoot << "->v"
                    << Desert::Migration::kSceneVersionServiceAssetRoot << " (";
                if ( report.ServiceAssetRoot.Refs > 0 )
                    out << report.ServiceAssetRoot.Refs << " font/icon/video reference(s) root-tagged on "
                        << report.ServiceAssetRoot.Entities << " entity(ies), " << report.ServiceAssetRoot.Empty
                        << " of them an empty slot";
                else if ( report.ServiceAssetRoot.UnrootedNames.empty() )
                    out << "stamp only - no entity named a font, an icon or a video";
                else
                    out << "no reference could be root-tagged";
                // Named, not counted, like every step above that can refuse a value: a reference neither
                // root can place still does not resolve in a packaged game.
                for ( const auto& name : report.ServiceAssetRoot.UnrootedNames )
                    out << "; under NEITHER content root, carried over untagged: " << name;
                out << ")";
            }
            if ( report.RetiredKeysRaised )
            {
                // NOT a step's own pair of numbers, unlike every line above: the retirement pass is
                // gated on the head and sweeps a file from WHEREVER it was to wherever the head is now
                // (see MigrateRetiredKeys). Printing a fixed 13->14 here would have reported the wrong
                // transition for every file K3 converted, which stood at 14.
                out << " retired keys -> v" << Desert::Migration::kSceneVersion << " (";
                if ( report.RetiredKeys.KeysRemoved > 0 )
                {
                    // Named with their values AND the reason, because from v14 on this is the ONLY way a
                    // key ever leaves a file: the saver preserves everything it does not declare, so a
                    // removal is always a decision somebody made and the operator is entitled to see it.
                    out << report.RetiredKeys.KeysRemoved << " retired key(s) removed:";
                    for ( const auto& name : report.RetiredKeys.RemovedNames )
                        out << " " << name;
                }
                else
                {
                    out << "stamp only - the scene stated no retired key";
                }
                if ( canonical.Refused )
                {
                    out << "; Settings NOT canonicalised - see the error above";
                }
                else
                {
                    out << "; Settings canonical (";
                    if ( canonical.BlockCreated )
                        out << "block created, ";
                    out << canonical.KeysAdded << " field(s) the file did not state, " << canonical.ValuesRestated
                        << " restated at float precision)";
                }
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
                // Under the SAME root MigrateScene was measured against, one page above: the relative path
                // inside the scene and the file on disk must agree about one root or the scene names a
                // material that is not where it says. That root is the SCENE'S (SceneOutputRoot) and not
                // the process's working directory — see the header for what the working directory cost.
                const std::filesystem::path matPath = ( assetsRoot / mat.RelativePath ).lexically_normal();

                // TWO SCENES MUST NOT LAND ON ONE MATERIAL FILE. The name is derived from the scene's
                // SceneName, which is NOT unique by construction — a .desce copied from another and
                // edited keeps the original's name, and this repository's own verification protocol
                // relies on exactly that copying. Two such scenes would produce one path here, the
                // second write would take the first's look, and BOTH scenes would then name a file that
                // describes only one of them: a silent whole-sky loss with nothing in the log. The
                // migration function is pure and per-scene, so it cannot see the collision; this loop is
                // the only place in the run that can. Named and fatal, never resolved by guessing at a
                // suffix — the fix is to give the scene its own SceneName, which is what the operator
                // has to know.
                const auto claimed = writtenMaterials.emplace( matPath.generic_string(), path.string() );
                if ( !claimed.second && claimed.first->second != path.string() )
                {
                    err << "FAIL   " << path.string() << " — its cloud material would be written to "
                        << matPath.string() << ", which " << claimed.first->second
                        << " already claimed in this run: both scenes state the same SceneName. Give one "
                        << "of them its own name and re-run; neither scene is modified.\n";
                    materialsFailed = true;
                    break;
                }

                std::error_code ec;
                std::filesystem::create_directories( matPath.parent_path(), ec );
                if ( !Common::Utils::FileSystem::WriteContentToFileAtomic( matPath, mat.Json ) )
                {
                    err << "FAIL   " << matPath.string() << " — the cloud material could not be written; "
                        << path.string() << " is left at its old version\n";
                    materialsFailed = true;
                    break;
                }
                // The FULL path, not the relative name it used to print. An operator reading "wrote
                // Materials/M_X.demat" cannot tell which of two trees it landed in, which is precisely the
                // question this defect turned on; the line now answers it.
                out << "        wrote " << matPath.string() << "\n";
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

        // THE MATERIALS, AFTER the scenes — a scene's v11 -> v12 raise WRITES `.demat` files, and those
        // are already produced with the current slot names (MigrateCloudMaterialV11ToV12 calls the same
        // step), so this pass finds nothing to do in them and says so. Running it first would depend on
        // whether the file existed yet, which is an ordering nobody should have to know about.
        int materialsChanged = 0;
        for ( const auto& path : materials )
        {
            const std::string source = ReadAll( path );
            if ( source.empty() )
            {
                err << "FAIL   " << path.string() << " — unreadable or empty\n";
                ++failed;
                continue;
            }

            auto parsed = rfl::json::read<Desert::Assets::MaterialData>( source );
            if ( !parsed )
            {
                err << "FAIL   " << path.string() << " — " << parsed.error().what() << "\n";
                ++failed;
                continue;
            }

            const Desert::Migration::CloudMaterialLayoutReport report =
                 Desert::Migration::MigrateCloudMaterialLayoutInputs( parsed.value() );
            // BOTH STEPS ALWAYS RUN, and neither short-circuits the other: a `.demat` can need the albedo
            // raise without ever having had a `CloudLayout` binding, and returning early on the first
            // report is how a file gets certified "ok" while still carrying a scalar albedo — which
            // renders as a RED sky, not as a missing feature.
            const Desert::Migration::CloudMaterialAlbedoReport albedo =
                 Desert::Migration::MigrateCloudMaterialAlbedoToColour( parsed.value() );

            if ( !report.Changed() && !albedo.Changed() )
            {
                out << "ok     " << path.string() << " — no pre-O-4 layout slot, no scalar albedo\n";
                continue;
            }

            // WHAT WAS RAISED, phrased ONCE and printed only where it is true. It used to be printed
            // before the write, which meant a refused write reported "raised <file>" and then "FAIL
            // <file>" — a claim of an action that had not happened, beside the correction. That is the
            // Д31 class with its sign flipped, and it was found by making the write refuse on purpose.
            std::ostringstream what;
            if ( report.Changed() )
                what << " " << report.Split
                     << " CloudLayout binding(s) split into LayoutPattern + LayoutMask, both naming the "
                        "same painting;";
            if ( albedo.Changed() )
                what << " " << albedo.Broadcast
                     << " scalar ScatteringAlbedo value(s) broadcast to a neutral colour;";

            if ( check )
            {
                out << "WOULD  " << path.string() << " —" << what.str() << "\n";
                ++materialsChanged;
                continue;
            }

            if ( !Common::Utils::FileSystem::WriteContentToFileAtomic( path, rfl::json::write( parsed.value() ) ) )
            {
                err << "FAIL   " << path.string() << " — the raise could not be written; the original file "
                    << "is untouched. It would have been:" << what.str() << "\n";
                ++failed;
                continue;
            }
            out << "raised " << path.string() << " —" << what.str() << "\n";
            ++materialsChanged;
        }

        out << "SceneMigrator: " << scenes.size() << " scene(s), " << changed
            << ( check ? " would change, " : " raised, " ) << materials.size() << " material(s), "
            << materialsChanged << ( check ? " would change, " : " raised, " ) << failed << " failed\n";

        if ( failed > 0 )
            return 1;
        return ( check && ( changed > 0 || materialsChanged > 0 ) ) ? 1 : 0;
    }
} // namespace Desert::Migration
