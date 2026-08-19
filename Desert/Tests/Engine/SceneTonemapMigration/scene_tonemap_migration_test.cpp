// The tonemapper migration, scene v1 -> v2, and the relation it exists to protect.
//
// The engine's default tonemapping operator changed from extended Reinhard to ACES (decision D-10,
// Docs/Clouds/ANALYSIS_APPROACH.md §7). A default change is the one kind of change that rewrites every
// file that never mentioned the setting: a scene whose Exposure and White Point were dialled in against
// Reinhard would be re-graded by an operator its author never chose, and nothing in the file would say
// so. That is what this migration prevents - it PINS the operator each existing scene was authored on,
// so the only scenes that move to ACES are the ones somebody moved deliberately.
//
// Two things are therefore asserted here, and they pull in opposite directions on purpose:
//
//   1. A scene that predates the field comes out of the migration on REINHARD - its picture is
//      byte-identical to what it was before the default moved.
//   2. The repository's own scenes carry ACES, because this task re-authored them, and the C++ default
//      is ACES, because that is what a NEW scene must be. If those two ever disagree the programme is
//      measuring itself against a UE frame through a different curve, which is the whole defect D-10
//      was raised to remove.
//
// Everything below runs on the parsed tree. No GPU, no scene graph, no asset manager - the migration is
// a pure function and this is what that buys.

#include <Engine/Core/SceneSettings.hpp>
#include <Engine/Core/Serialize/SceneMigration.hpp>

#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionSerializer.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Desert::Core::kSceneVersion;
using Desert::Core::kSceneVersionSky;
using Desert::Core::kSceneVersionTonemap;
using Desert::Core::MigrateScene;
using Desert::Core::MigrateTonemapperV1ToV2;
using Desert::Core::SceneSerialized;
using Desert::Core::SceneSettings;
using Desert::Core::TonemapMigrationReport;
using Desert::Core::TonemapOperator;
using Desert::Reflection::ReflectionRegistry;

namespace
{
    constexpr const char* kTonemapperKey = "Tonemapper";

    // The post-processing half of Editor/Resources/Assets/Scenes/Clouds_Demo.desce as it stood at scene
    // v1: an exposure of 0.22 against a sun of 22, and a White Point of 8. Both numbers were chosen by
    // looking at a Reinhard frame, which is exactly why the migration must not hand them to ACES.
    rfl::Generic::Object RealCloudsDemoSettings()
    {
        rfl::Generic::Object o;
        o["Exposure"]     = 0.2199999988079071;
        o["Gamma"]        = 2.200000047683716;
        o["WhitePoint"]   = 8.0;
        o["AutoExposure"] = false;
        o["EnableBloom"]  = true;
        return o;
    }

    // The settings block after the migration, whatever shape it started in.
    rfl::Generic::Object MigratedSettings( std::optional<rfl::Generic>& settings )
    {
        EXPECT_TRUE( settings.has_value() );
        const auto object = settings->to_object();
        EXPECT_TRUE( object.has_value() );
        return object.value_or( rfl::Generic::Object{} );
    }

    // The operator the reflected SceneSettings actually ends up with - the value the renderer will read,
    // not the value the JSON happens to spell. Asserting on the payload alone would pass even if the
    // serializer refused the key.
    TonemapOperator LoadedOperator( const rfl::Generic::Object& settings )
    {
        SceneSettings loaded;
        const auto*   type = ReflectionRegistry::Get().Find( "SceneSettings" );
        EXPECT_NE( type, nullptr );
        if ( type )
            Desert::Reflection::DeserializeReflected( *type, &loaded, settings );
        return loaded.Tonemapper;
    }

    // The repository root, found by walking up from wherever the binary was started - the same approach
    // SettingConsumers uses, so neither test has to be run from one exact directory.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path, std::ios::binary );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Every .desce the repository ships. Listed by name rather than discovered, so DELETING a scene from
    // the list is a visible edit and adding one without converting it is a red test.
    constexpr const char* kRepositoryScenes[] = {
         "Clouds_Demo.desce",          "Clouds_Showcase.desce", "Clouds_Sunset.desce", "CornellDemo.desce",
         "DepthPrecisionProbe.desce",  "Desert_Sandbox.desce",  "Fog_Showcase.desce",  "MainMenu.desce",
         "Sky_PhysicalShowcase.desce", "Starter.desce",         "Terrain_Grass.desce",
    };
} // namespace

// ----------------------------------------------------------------------------------------------------
// The pure function
// ----------------------------------------------------------------------------------------------------

// The ordinary case: a scene that carries settings but has never heard of the operator keeps its picture.
TEST( SceneTonemapMigration, PinsReinhardOnASceneThatPredatesTheField )
{
    std::optional<rfl::Generic> settings = rfl::Generic( RealCloudsDemoSettings() );

    const TonemapMigrationReport report = MigrateTonemapperV1ToV2( settings );

    EXPECT_TRUE( report.OperatorPinned );
    EXPECT_FALSE( report.SettingsCreated );

    const auto migrated = MigratedSettings( settings );
    EXPECT_EQ( LoadedOperator( migrated ), TonemapOperator::Reinhard );

    // Nothing else moved. The exposure and white point are the two numbers a wrong migration would
    // quietly re-purpose, so they are named rather than counted.
    EXPECT_EQ( migrated.size(), RealCloudsDemoSettings().size() + 1 );
    EXPECT_DOUBLE_EQ( migrated.get( "Exposure" ).value().to_double().value(), 0.2199999988079071 );
    EXPECT_DOUBLE_EQ( migrated.get( "WhitePoint" ).value().to_double().value(), 8.0 );
}

// MainMenu.desce has no "Settings" key at all. Left alone it would load every field at its C++ default,
// which after D-10 means ACES - the silent re-grade this migration exists to stop.
TEST( SceneTonemapMigration, CreatesASettingsBlockWhenTheSceneHasNone )
{
    std::optional<rfl::Generic> settings; // absent, exactly as the file has it

    const TonemapMigrationReport report = MigrateTonemapperV1ToV2( settings );

    EXPECT_TRUE( report.SettingsCreated );
    EXPECT_TRUE( report.OperatorPinned );

    const auto migrated = MigratedSettings( settings );
    EXPECT_EQ( migrated.size(), std::size_t{ 1 } ); // only the operator; every other field keeps its default
    EXPECT_EQ( LoadedOperator( migrated ), TonemapOperator::Reinhard );
}

// A scene that already states its operator states it for a reason. Re-running the migration over an
// already-raised tree must be a no-op, because a tree can be re-read (undo, a second load) at any time.
TEST( SceneTonemapMigration, LeavesAnAlreadyStatedOperatorAlone )
{
    rfl::Generic::Object authored = RealCloudsDemoSettings();
    authored[kTonemapperKey]      = static_cast<int64_t>( TonemapOperator::ACES );

    std::optional<rfl::Generic>  settings = rfl::Generic( authored );
    const TonemapMigrationReport report   = MigrateTonemapperV1ToV2( settings );

    EXPECT_FALSE( report.OperatorPinned );
    EXPECT_FALSE( report.SettingsCreated );
    EXPECT_EQ( LoadedOperator( MigratedSettings( settings ) ), TonemapOperator::ACES );
}

TEST( SceneTonemapMigration, IsIdempotent )
{
    std::optional<rfl::Generic> settings = rfl::Generic( RealCloudsDemoSettings() );

    MigrateTonemapperV1ToV2( settings );
    const std::string once = rfl::json::write( settings.value() );

    const TonemapMigrationReport second = MigrateTonemapperV1ToV2( settings );
    EXPECT_FALSE( second.OperatorPinned );
    EXPECT_EQ( rfl::json::write( settings.value() ), once );
}

// A corrupt settings payload is reported (LOG_WARN) and left exactly as it was. Replacing it with a
// fresh object would throw away every other scene-wide value in the file to save one of them.
TEST( SceneTonemapMigration, RefusesASettingsPayloadThatIsNotAnObject )
{
    std::optional<rfl::Generic> settings = rfl::Generic( std::string( "not an object" ) );

    const TonemapMigrationReport report = MigrateTonemapperV1ToV2( settings );

    EXPECT_FALSE( report.OperatorPinned );
    EXPECT_FALSE( report.SettingsCreated );
    ASSERT_TRUE( settings.has_value() );
    EXPECT_EQ( settings->to_string().value_or( "" ), "not an object" );
}

// ----------------------------------------------------------------------------------------------------
// The version gate
// ----------------------------------------------------------------------------------------------------

// The two scene migrations must not share one threshold. Before this task kSceneVersion WAS the sky
// migration's target, so raising it to 2 would have re-run the sky migration over every v1 file in the
// repository and reported it as a schema move that never happened.
TEST( SceneTonemapMigration, RaisingAV1SceneRunsTheTonemapperMigrationAndNotTheSkyOne )
{
    SceneSerialized scene;
    scene.SceneName    = "v1 scene";
    scene.SceneVersion = kSceneVersionSky;
    scene.UnitVersion  = 1;
    scene.Settings     = rfl::Generic( RealCloudsDemoSettings() );

    const auto report = MigrateScene( scene );

    EXPECT_FALSE( report.SkyRaised );
    EXPECT_FALSE( report.UnitsRaised );
    EXPECT_TRUE( report.TonemapperRaised );
    EXPECT_TRUE( report.Tonemap.OperatorPinned );
    EXPECT_EQ( scene.SceneVersion.value(), kSceneVersion );
    EXPECT_EQ( LoadedOperator( MigratedSettings( scene.Settings ) ), TonemapOperator::Reinhard );
}

// A file that predates BOTH goes through both, in one pass, and comes out stamped at the current
// generation - so nothing migrates it a second time.
TEST( SceneTonemapMigration, AV0SceneIsRaisedAllTheWay )
{
    SceneSerialized scene;
    scene.SceneName = "v0 scene";
    scene.Settings  = rfl::Generic( RealCloudsDemoSettings() );

    const auto report = MigrateScene( scene );

    EXPECT_TRUE( report.SkyRaised );
    EXPECT_TRUE( report.UnitsRaised );
    EXPECT_TRUE( report.TonemapperRaised );
    EXPECT_EQ( scene.SceneVersion.value(), kSceneVersion );

    const auto again = MigrateScene( scene );
    EXPECT_FALSE( again.Changed() );
}

TEST( SceneTonemapMigration, TheTonemapperGenerationIsTheCurrentOne )
{
    // States the relation the gate above depends on: the tonemapper migration is the newest one, so
    // kSceneVersion is its target. A third migration added without moving this line would be gated on a
    // version the loader already stamps, and would therefore never run.
    EXPECT_EQ( kSceneVersion, kSceneVersionTonemap );
    EXPECT_GT( kSceneVersionTonemap, kSceneVersionSky );
}

// ----------------------------------------------------------------------------------------------------
// The relation D-10 is: ACES is the default, and the repository is on it
// ----------------------------------------------------------------------------------------------------

TEST( SceneTonemapMigration, ACESIsTheDefaultOperator )
{
    EXPECT_EQ( SceneSettings{}.Tonemapper, TonemapOperator::ACES );

    // An empty settings block means "every field at its default", so this is the value a scene authored
    // from now on gets without saying anything.
    EXPECT_EQ( LoadedOperator( rfl::Generic::Object{} ), TonemapOperator::ACES );
}

// The repository's scenes were re-authored onto ACES by this task. This is the assertion that keeps them
// there: a scene re-saved by an editor build that had lost the change would come back on Reinhard, and
// every calibration number measured after that would be measuring the wrong curve.
TEST( SceneTonemapMigration, EveryRepositorySceneIsStampedAndOnACES )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "could not locate the repository root from the working directory";

    for ( const char* name : kRepositoryScenes )
    {
        const std::string path = root + "Editor/Resources/Assets/Scenes/" + name;
        const std::string text = ReadFile( path );
        ASSERT_FALSE( text.empty() ) << path << " is missing or empty";

        auto parsed = rfl::json::read<SceneSerialized>( text );
        ASSERT_TRUE( parsed ) << path << ": " << parsed.error().what();

        EXPECT_EQ( parsed.value().SceneVersion.value_or( 0 ), kSceneVersion )
             << path << " is not stamped at the current scene generation - run Tools/SceneMigrator";

        ASSERT_TRUE( parsed.value().Settings.has_value() ) << path << " carries no Settings block";
        const auto settings = parsed.value().Settings->to_object();
        ASSERT_TRUE( settings.has_value() ) << path << ": Settings is not an object";

        EXPECT_EQ( LoadedOperator( settings.value() ), TonemapOperator::ACES ) << path << " is not on ACES";
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
