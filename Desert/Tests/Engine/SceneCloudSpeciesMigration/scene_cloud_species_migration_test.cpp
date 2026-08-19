// The cloud-species migration, scene v3 -> v4, and the relations it exists to protect.
//
// The vertical profile stopped being one analytic curve driven by a scalar and became a per-SPECIES
// table, so a scene file has to name a species instead of a number. Four keys leave the file and one
// arrives:
//
//   * "CloudType" BECOMES "Species". The old scalar ran from "flat sheet low in the layer" to "tall heaped
//     cloud" and the library is ordered along exactly that axis, so the translation is a partition of the
//     scalar rather than a guess. The boundaries are placed so that the component's own former default of
//     0.6 lands on the species the new default names — a scene that carried the default comes out looking
//     like what it was.
//   * "CloudTypeVariance" is DROPPED. It mixed noise into the scalar so that neighbouring clouds would not
//     all reach the same ceiling; the table's second axis is the placement pattern's own value, which
//     answers the same question with height that CORRELATES with how much cloud is there. There is nothing
//     on the component for it to become.
//   * "LayerBottomAltitude" and "LayerThickness" are DROPPED. The shell is now the union of the altitude
//     ranges of the species in the layer and is computed by Graphic::PackCloudParams. An authored shell and
//     a species' own altitudes are two numbers obliged to agree, which is the defect class the whole move
//     removes; carrying the authored pair forward would reintroduce it under a new name.
//
// Everything below runs on the parsed tree. No GPU, no scene graph, no asset manager — the migration is a
// pure function and this is what that buys.

#include <Engine/Core/Serialize/SceneMigration.hpp>
#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/Clouds/CloudProfileTable.hpp>

#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using Desert::Core::CloudSpeciesMigrationReport;
using Desert::Core::kSceneVersion;
using Desert::Core::kSceneVersionCloudNoise;
using Desert::Core::kSceneVersionCloudSpecies;
using Desert::Core::MigrateCloudSpeciesV3ToV4;
using Desert::Core::MigrateScene;
using Desert::Core::SceneSerialized;
using Desert::Graphic::CloudSpecies;
using Desert::Reflection::ReflectionRegistry;

namespace
{
    // The three keys with nowhere to go, as a v3 file spells them.
    const std::vector<std::string> kDroppedKeys = { "LayerBottomAltitude", "LayerThickness", "CloudTypeVariance" };

    // A v3 cloud payload with everything the migration touches present, and a representative selection of
    // the settings that STAY, so the test can assert that nothing else moved.
    rfl::Generic::Object CloudPayloadV3( double cloudType )
    {
        rfl::Generic::Object o;
        o["Enabled"]             = true;
        o["LayerBottomAltitude"] = 300000.0;
        o["LayerThickness"]      = 500000.0;
        o["Coverage"]            = 0.25;
        o["CloudType"]           = cloudType;
        o["CloudTypeVariance"]   = 0.5;
        o["WeatherTileSize"]     = 1200000.0;
        o["DetailStrength"]      = 0.10000000149011612;
        o["ExtinctionScale"]     = 8.0;
        return o;
    }

    Desert::Assets::EntityData EntityWithClouds( double cloudType )
    {
        Desert::Assets::EntityData entity;
        entity.Tag                           = "Sky";
        entity.Components["VolumetricCloud"] = rfl::Generic( CloudPayloadV3( cloudType ) );
        return entity;
    }

    rfl::Generic::Object CloudPayloadOf( const Desert::Assets::EntityData& entity )
    {
        const auto payload = entity.Components.get( "VolumetricCloud" );
        EXPECT_TRUE( payload.has_value() );
        const auto object = payload.value().to_object();
        EXPECT_TRUE( object.has_value() );
        return object.value_or( rfl::Generic::Object{} );
    }

    bool Has( const rfl::Generic::Object& object, const std::string& key )
    {
        return object.get( key ).has_value();
    }

    // The species a migrated payload names, or -1 when it names none.
    int SpeciesOf( const rfl::Generic::Object& object )
    {
        const auto value = object.get( "Species" );
        if ( !value.has_value() )
            return -1;
        return static_cast<int>( value.value().to_int64().value_or( -1 ) );
    }
} // namespace

TEST( SceneCloudSpeciesMigration, TheFourFieldsGoAndTheSpeciesArrives )
{
    std::vector<Desert::Assets::EntityData> entities{ EntityWithClouds( 0.55 ) };

    const CloudSpeciesMigrationReport report = MigrateCloudSpeciesV3ToV4( entities );

    EXPECT_EQ( report.Entities, 1 );
    EXPECT_EQ( report.FieldsDropped, 4 ); // the three above plus CloudType, which became the species
    EXPECT_EQ( report.SpeciesSet, 1 );

    const rfl::Generic::Object migrated = CloudPayloadOf( entities.front() );

    for ( const std::string& key : kDroppedKeys )
        EXPECT_FALSE( Has( migrated, key ) ) << key << " survived the migration";
    EXPECT_FALSE( Has( migrated, "CloudType" ) ) << "the scalar cloud type survived the migration";

    EXPECT_TRUE( Has( migrated, "Species" ) );
}

TEST( SceneCloudSpeciesMigration, TheScalarPicksTheSpeciesAlongTheAxisItAlreadyRanOn )
{
    // The old scalar's own documentation: "0 is a flat sheet lying in the bottom quarter of the layer; 1
    // is a heaped cloud that fills it". The library is ordered along that axis, so every boundary below is
    // a translation and not a preference — and 0.6, the component's former default, has to land on the
    // species the new default names, or every scene that carried the default changes appearance for no
    // reason its author would recognise.
    struct Case
    {
        double       Scalar;
        CloudSpecies Expected;
    };

    const Case cases[] = {
         { 0.0, CloudSpecies::Stratus },           { 0.24, CloudSpecies::Stratus },
         { 0.25, CloudSpecies::CumulusMediocris }, { 0.45, CloudSpecies::CumulusMediocris },
         { 0.55, CloudSpecies::CumulusCongestus }, { 0.60, CloudSpecies::CumulusCongestus },
         { 0.75, CloudSpecies::CumulusCongestus }, { 0.85, CloudSpecies::Cumulonimbus },
         { 1.0, CloudSpecies::Cumulonimbus },
    };

    for ( const Case& c : cases )
    {
        std::vector<Desert::Assets::EntityData> entities{ EntityWithClouds( c.Scalar ) };
        MigrateCloudSpeciesV3ToV4( entities );

        EXPECT_EQ( SpeciesOf( CloudPayloadOf( entities.front() ) ), static_cast<int>( c.Expected ) )
             << "cloud type " << c.Scalar << " chose the wrong species";
    }

    // The default of the component the migration is FOR, stated as its own assertion because it is the
    // one case where "which species" is decided by something outside this function.
    Desert::ECS::VolumetricCloudData        fresh;
    std::vector<Desert::Assets::EntityData> entities{ EntityWithClouds( 0.6 ) };
    MigrateCloudSpeciesV3ToV4( entities );
    EXPECT_EQ( SpeciesOf( CloudPayloadOf( entities.front() ) ), static_cast<int>( fresh.Species ) )
         << "a scene carrying the old default no longer carries the new one";
}

TEST( SceneCloudSpeciesMigration, EverySettingThatStaysIsUntouched )
{
    // The failure mode a "rebuild the object" migration has, and the reason it is worth a test of its own:
    // rebuilding is how you accidentally drop the keys you meant to keep, and the symptom would be a cloud
    // layer that silently returned to its defaults on load.
    std::vector<Desert::Assets::EntityData> entities{ EntityWithClouds( 0.55 ) };
    MigrateCloudSpeciesV3ToV4( entities );

    const rfl::Generic::Object migrated = CloudPayloadOf( entities.front() );

    EXPECT_TRUE( Has( migrated, "Enabled" ) );
    EXPECT_TRUE( Has( migrated, "Coverage" ) );
    EXPECT_TRUE( Has( migrated, "WeatherTileSize" ) );
    EXPECT_TRUE( Has( migrated, "DetailStrength" ) );
    EXPECT_TRUE( Has( migrated, "ExtinctionScale" ) );

    // Four keys out, one in.
    EXPECT_EQ( migrated.size(), CloudPayloadV3( 0.55 ).size() - 3u );

    const auto coverage = migrated.get( "Coverage" );
    ASSERT_TRUE( coverage.has_value() );
    EXPECT_DOUBLE_EQ( coverage.value().to_double().value_or( -1.0 ), 0.25 );
}

TEST( SceneCloudSpeciesMigration, APayloadWithNoCloudTypeKeepsTheDefaultSpeciesRatherThanInventingOne )
{
    // An absent key is how the reflection serializer spells "keep the C++ default". A file that never said
    // anything about its cloud type is a file whose author expressed no preference, and writing Stratus
    // into it would be a guess about intent dressed up as a migration.
    rfl::Generic::Object payload;
    payload["Enabled"]        = true;
    payload["LayerThickness"] = 500000.0;

    Desert::Assets::EntityData entity;
    entity.Tag                           = "Sky";
    entity.Components["VolumetricCloud"] = rfl::Generic( payload );

    std::vector<Desert::Assets::EntityData> entities{ entity };
    const CloudSpeciesMigrationReport       report = MigrateCloudSpeciesV3ToV4( entities );

    EXPECT_EQ( report.Entities, 1 );
    EXPECT_EQ( report.FieldsDropped, 1 );
    EXPECT_EQ( report.SpeciesSet, 0 );
    EXPECT_FALSE( Has( CloudPayloadOf( entities.front() ), "Species" ) );
}

TEST( SceneCloudSpeciesMigration, ACloudTypeThatIsNotANumberIsSurvivedRatherThanGuessedAt )
{
    // One hand-edited scene away, and the answer is the C++ default plus a warning rather than a number
    // pulled out of a string.
    rfl::Generic::Object payload;
    payload["Enabled"]   = true;
    payload["CloudType"] = std::string( "cumulus" );

    Desert::Assets::EntityData entity;
    entity.Tag                           = "Sky";
    entity.Components["VolumetricCloud"] = rfl::Generic( payload );

    std::vector<Desert::Assets::EntityData> entities{ entity };
    const CloudSpeciesMigrationReport       report = MigrateCloudSpeciesV3ToV4( entities );

    EXPECT_EQ( report.Entities, 1 );
    EXPECT_EQ( report.SpeciesSet, 0 );
    EXPECT_FALSE( Has( CloudPayloadOf( entities.front() ), "Species" ) );
    EXPECT_FALSE( Has( CloudPayloadOf( entities.front() ), "CloudType" ) );
}

TEST( SceneCloudSpeciesMigration, ItIsIdempotent )
{
    std::vector<Desert::Assets::EntityData> entities{ EntityWithClouds( 0.55 ) };
    MigrateCloudSpeciesV3ToV4( entities );

    const CloudSpeciesMigrationReport second = MigrateCloudSpeciesV3ToV4( entities );
    EXPECT_EQ( second.Entities, 0 );
    EXPECT_EQ( second.FieldsDropped, 0 );
    EXPECT_EQ( second.SpeciesSet, 0 );

    // And the species it wrote the first time is still the one it wrote.
    EXPECT_EQ( SpeciesOf( CloudPayloadOf( entities.front() ) ),
               static_cast<int>( CloudSpecies::CumulusCongestus ) );
}

TEST( SceneCloudSpeciesMigration, AnEntityWithNoCloudLayerIsLeftAlone )
{
    Desert::Assets::EntityData plain;
    plain.Tag                      = "Cube";
    plain.Components["StaticMesh"] = rfl::Generic( rfl::Generic::Object{} );

    std::vector<Desert::Assets::EntityData> entities{ plain };
    const CloudSpeciesMigrationReport       report = MigrateCloudSpeciesV3ToV4( entities );

    EXPECT_EQ( report.Entities, 0 );
    EXPECT_TRUE( entities.front().Components.get( "StaticMesh" ).has_value() );
}

TEST( SceneCloudSpeciesMigration, ACloudPayloadThatIsNotAnObjectIsSurvivedRatherThanCrashedOn )
{
    Desert::Assets::EntityData broken;
    broken.Tag                           = "Sky";
    broken.Components["VolumetricCloud"] = rfl::Generic( 7.0 );

    std::vector<Desert::Assets::EntityData> entities{ broken };
    const CloudSpeciesMigrationReport       report = MigrateCloudSpeciesV3ToV4( entities );

    EXPECT_EQ( report.Entities, 0 );
    EXPECT_EQ( report.FieldsDropped, 0 );
}

TEST( SceneCloudSpeciesMigration, MigrateSceneRunsItAndStampsTheFileSoItNeverRunsAgain )
{
    SceneSerialized scene;
    scene.SceneName    = "Clouds";
    scene.SceneVersion = kSceneVersionCloudNoise;
    scene.UnitVersion  = Desert::Core::kUnitVersion;
    scene.Entities.push_back( EntityWithClouds( 0.55 ) );

    const auto report = MigrateScene( scene );

    EXPECT_TRUE( report.CloudSpeciesRaised );
    EXPECT_EQ( report.CloudSpecies.FieldsDropped, 4 );
    EXPECT_TRUE( report.Changed() );
    EXPECT_EQ( scene.SceneVersion.value_or( 0 ), kSceneVersion );
    EXPECT_EQ( kSceneVersion, kSceneVersionCloudSpecies );

    // Second pass over the stamped tree: nothing left to do.
    const auto again = MigrateScene( scene );
    EXPECT_FALSE( again.Changed() );
}

// ---------------------------------------------------------------------------------------------------
// The other half of "the old path is gone": the struct itself.
// ---------------------------------------------------------------------------------------------------

TEST( SceneCloudSpeciesMigration, TheReflectedComponentNoLongerCarriesWhatTheMigrationDeletes )
{
    const auto* type = ReflectionRegistry::Get().Find( "VolumetricCloudData" );
    ASSERT_NE( type, nullptr );

    for ( const std::string& key : { std::string( "LayerBottomAltitude" ), std::string( "LayerThickness" ),
                                     std::string( "CloudType" ), std::string( "CloudTypeVariance" ) } )
    {
        const bool present = std::any_of( type->Fields.begin(), type->Fields.end(),
                                          [&key]( const auto& field ) { return field.Name == key; } );
        EXPECT_FALSE( present ) << key
                                << " is still a reflected field, so the migration removes a value the "
                                   "struct can still hold — that is silent data loss";
    }
}

TEST( SceneCloudSpeciesMigration, TheReflectedComponentCarriesTheSpeciesThatReplacedThem )
{
    const auto* type = ReflectionRegistry::Get().Find( "VolumetricCloudData" );
    ASSERT_NE( type, nullptr );

    const auto slot = std::find_if( type->Fields.begin(), type->Fields.end(),
                                    []( const auto& field ) { return field.Name == "Species"; } );
    ASSERT_NE( slot, type->Fields.end() ) << "the component has no species, so nothing replaced the four "
                                             "fields the migration deletes";

    // It draws as a COMBO and not as a number, which is what an enumerated field buys and the reason the
    // scalar was not simply renamed: four named species an artist recognises, rather than a slider whose
    // useful positions have to be remembered.
    EXPECT_EQ( slot->Type, Desert::Reflection::FieldType::Enum );
    EXPECT_EQ( slot->EnumValues.size(), Desert::Graphic::kCloudSpeciesCount );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
