// The v13 -> v14 step: the keys this project RETIRED leave the files, and the Settings block becomes
// exactly what the engine's saver would write.
//
// WHY THE STEP EXISTS AT ALL, since nothing reads a retired key. Until K11 the answer was "the next save
// deletes it anyway" — the saver enumerated its own registry, so any key it did not declare evaporated on
// contact. That was the defect; removing it made this step necessary. A preserved key is preserved whether
// or not we still want it, so wanting rid of one is now a DECISION somebody writes down, and
// Migration::kRetiredKeys is the only place in the repository where that is written.
//
// "Retired" is therefore not "unknown", and the difference has to be in the code rather than in somebody's
// head: an unknown key is another build's and is kept (and named in the load's log); a retired key is ours,
// is dead, and is removed here, once, from the files.
//
// What is asserted:
//
//   1. Every row of kRetiredKeys is removed from the block it names, with its value and reason reported,
//      and nothing else in the block is touched or reordered.
//   2. The step is idempotent and gated on its own version integer through MigrateScene.
//   3. Canonicalisation produces the SAVER's bytes — the same fields, in the same order, through the same
//      reflection table — and keeps an AssetHandle's on-disk form verbatim.
//   4. THE HEAD ASSERTION, which lives in exactly one suite at a time and has moved here.
//   5. THE RELATION: no retired key is still a reflected setting. A row that named a live field would
//      delete authored data on every run, and the live table is the only thing that can say.

#include <SceneMigration.hpp>
#include <SettingsCanonical.hpp>

#include <Engine/Core/SceneSettings.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace Desert;

namespace
{
    rfl::Generic Settings( const std::string& json )
    {
        const auto parsed = rfl::json::read<rfl::Generic>( json );
        EXPECT_TRUE( parsed.has_value() ) << json;
        return parsed.has_value() ? parsed.value() : rfl::Generic();
    }

    std::vector<std::string> KeysOf( const std::optional<rfl::Generic>& settings )
    {
        std::vector<std::string> keys;
        if ( !settings.has_value() )
            return keys;
        if ( const auto object = settings.value().to_object(); object.has_value() )
            for ( const auto& [key, value] : object.value() )
                keys.push_back( key );
        return keys;
    }

    bool Has( const std::optional<rfl::Generic>& settings, const std::string& key )
    {
        const std::vector<std::string> keys = KeysOf( settings );
        return std::find( keys.begin(), keys.end(), key ) != keys.end();
    }

    std::string ValueOf( const std::optional<rfl::Generic>& settings, const std::string& key )
    {
        if ( !settings.has_value() )
            return {};
        if ( const auto object = settings.value().to_object(); object.has_value() )
            if ( const auto found = object.value().get( key ); found.has_value() )
                return rfl::json::write( found.value() );
        return {};
    }
} // namespace

// ── the retirement ───────────────────────────────────────────────────────────────────────────────

TEST( SceneRetiredKeysMigration, EveryRetiredKeyIsRemovedFromTheBlockItNamesAndReportedWithItsValue )
{
    std::optional<rfl::Generic>     settings = Settings( R"({"Exposure":1.0,"EnableSSGI":true,"Gamma":2.2})" );
    std::vector<Assets::EntityData> entities;

    const auto report = Migration::MigrateRetiredKeysV13ToV14( settings, entities );

    EXPECT_EQ( report.KeysRemoved, 1 );
    ASSERT_EQ( report.RemovedNames.size(), 1u );
    // The name, the VALUE it held, and the reason - not a count. Somebody authored that `true`, and the
    // sentence in the log is what tells them why it is gone (§1.4).
    EXPECT_NE( report.RemovedNames[0].find( "Settings.EnableSSGI=" ), std::string::npos )
         << report.RemovedNames[0];
    EXPECT_NE( report.RemovedNames[0].find( "GlobalIllumination" ), std::string::npos )
         << "the report does not say WHY the key is gone: " << report.RemovedNames[0];

    EXPECT_FALSE( Has( settings, "EnableSSGI" ) );
    // Order and content of everything else, because this file is compared byte for byte now.
    EXPECT_EQ( KeysOf( settings ), ( std::vector<std::string>{ "Exposure", "Gamma" } ) );
}

TEST( SceneRetiredKeysMigration, ABlockStatingNoRetiredKeyIsLeftByteIdentical )
{
    std::optional<rfl::Generic>     settings = Settings( R"({"Exposure":1.0,"Gamma":2.2})" );
    std::vector<Assets::EntityData> entities;
    const std::string               before = rfl::json::write( settings.value() );

    const auto report = Migration::MigrateRetiredKeysV13ToV14( settings, entities );

    EXPECT_EQ( report.KeysRemoved, 0 );
    EXPECT_EQ( rfl::json::write( settings.value() ), before );
}

TEST( SceneRetiredKeysMigration, AMissingSettingsBlockIsNotAFailure )
{
    std::optional<rfl::Generic>     settings; // a scene that states no settings at all
    std::vector<Assets::EntityData> entities;
    const auto                      report = Migration::MigrateRetiredKeysV13ToV14( settings, entities );
    EXPECT_EQ( report.KeysRemoved, 0 );
    EXPECT_FALSE( settings.has_value() );
}

TEST( SceneRetiredKeysMigration, TheStepIsGatedOnItsOwnVersionAndIsIdempotent )
{
    Core::SceneSerialized scene;
    scene.SceneVersion = Migration::kSceneVersionDebugView; // v13: the step must run
    scene.UnitVersion  = Core::kUnitVersion;
    scene.Settings     = Settings( R"({"EnableSSGI":true})" );

    const auto first = Migration::MigrateScene( scene );
    EXPECT_TRUE( first.RetiredKeysRaised );
    EXPECT_EQ( first.RetiredKeys.KeysRemoved, 1 );
    EXPECT_FALSE( Has( scene.Settings, "EnableSSGI" ) );
    EXPECT_EQ( scene.SceneVersion.value_or( 0 ), Core::kSceneVersion );

    // Hand-edited back in; the gate must not care, because the file now claims v14.
    scene.Settings    = Settings( R"({"EnableSSGI":true})" );
    const auto second = Migration::MigrateScene( scene );
    EXPECT_FALSE( second.RetiredKeysRaised );
    EXPECT_FALSE( second.Changed() );
    EXPECT_TRUE( Has( scene.Settings, "EnableSSGI" ) );
}

// ── canonicalisation ─────────────────────────────────────────────────────────────────────────────

TEST( SceneRetiredKeysMigration, CanonicalisationStatesEveryFieldTheSaverWouldStateInTheSaversOrder )
{
    const auto* type = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" );
    ASSERT_NE( type, nullptr );

    std::optional<rfl::Generic> settings = Settings( R"({"Exposure":0.26})" );
    const auto                  report   = Migration::CanonicaliseSettings( settings );

    ASSERT_FALSE( report.Refused );
    std::vector<std::string> expected;
    for ( const auto& field : type->Fields )
        expected.push_back( field.Name );
    EXPECT_EQ( KeysOf( settings ), expected )
         << "the canonical block is not the reflection table's field list, in its order";

    // `0.26` is not representable as a float, so the saver's own narrow-and-widen restates it. The VALUE
    // is unchanged - it is the same float either way - and only the TEXT moves. That is the whole reason
    // byte-identity needed a conversion in the files rather than a rule in code.
    EXPECT_EQ( report.ValuesRestated, 1 );
    EXPECT_NE( ValueOf( settings, "Exposure" ), "0.26" );
    EXPECT_FLOAT_EQ( std::stof( ValueOf( settings, "Exposure" ) ), 0.26f );
}

TEST( SceneRetiredKeysMigration, CanonicalisationIsIdempotent )
{
    std::optional<rfl::Generic> settings = Settings( R"({"Exposure":0.26})" );
    ASSERT_FALSE( Migration::CanonicaliseSettings( settings ).Refused );
    const std::string once = rfl::json::write( settings.value() );

    const auto again = Migration::CanonicaliseSettings( settings );
    EXPECT_EQ( again.KeysAdded, 0 );
    EXPECT_EQ( again.ValuesRestated, 0 );
    EXPECT_EQ( rfl::json::write( settings.value() ), once );
}

TEST( SceneRetiredKeysMigration, AnAssetHandleKeepsTheFormTheFileStatedIt )
{
    // An AssetHandle is written as a PATH when the saver has a resolver and as a raw integer when it does
    // not, and this tool has no AssetManager. Restating it would silently change the field's format in the
    // fourteen scenes that carry `"SplashSprite": ""`.
    const auto* type = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" );
    ASSERT_NE( type, nullptr );
    const bool anyHandle =
         std::any_of( type->Fields.begin(), type->Fields.end(), []( const Reflection::FieldInfo& f )
                      { return f.Type == Reflection::FieldType::AssetHandle; } );
    ASSERT_TRUE( anyHandle ) << "SceneSettings has no AssetHandle field, so this assertion is vacuous";

    std::optional<rfl::Generic> settings = Settings( R"({"SplashSprite":""})" );
    ASSERT_FALSE( Migration::CanonicaliseSettings( settings ).Refused );
    EXPECT_EQ( ValueOf( settings, "SplashSprite" ), R"("")" ) << "the path form was replaced by a raw handle";
}

TEST( SceneRetiredKeysMigration, ASceneWithNoSettingsBlockGetsTheCanonicalOne )
{
    // The saver always writes a Settings block, so a file without one can never be byte-stable.
    std::optional<rfl::Generic> settings;
    const auto                  report = Migration::CanonicaliseSettings( settings );
    EXPECT_TRUE( report.BlockCreated );
    ASSERT_TRUE( settings.has_value() );
    EXPECT_FALSE( KeysOf( settings ).empty() );
}

// ── the head, and the relation ───────────────────────────────────────────────────────────────────

// THE HEAD ASSERTION, which moved here from SceneDebugViewMigration because this is the newest step now.
// It is the run-time half of the static_assert in SceneMigration.hpp: if a step is ever added without
// raising Core::kSceneVersion the tool stamps files at a version the loader refuses, and every scene in
// the repository stops opening at once while each file looks correct in isolation.
TEST( SceneRetiredKeysMigration, ThisIsTheHeadStepAndItSitsAboveItsPredecessor )
{
    EXPECT_EQ( 14, Migration::kSceneVersionRetiredKeys );
    EXPECT_GT( Migration::kSceneVersionRetiredKeys, Migration::kSceneVersionDebugView );
    EXPECT_EQ( Migration::kSceneVersionRetiredKeys, Core::kSceneVersion )
         << "a newer step exists; move this assertion to that suite the way this one moved here";
}

// THE RELATION, and it is why this suite is worth more than its assertions: a row of kRetiredKeys names a
// key that must NOT be a live field. If one ever did, every run of the tool would delete authored data
// from every file that states it — and the only thing that can answer "is this still a field" is the live
// reflection table the saver writes from.
TEST( SceneRetiredKeysMigration, NoRetiredKeyIsStillAReflectedSceneSetting )
{
    const auto* type = Reflection::ReflectionRegistry::Get().Find( "SceneSettings" );
    ASSERT_NE( type, nullptr );

    for ( const Migration::RetiredKey& row : Migration::kRetiredKeys )
    {
        if ( std::string( row.Block ) != "Settings" )
            continue;
        const bool live = std::any_of( type->Fields.begin(), type->Fields.end(),
                                       [&row]( const Reflection::FieldInfo& f ) { return f.Name == row.Key; } );
        EXPECT_FALSE( live ) << "'" << row.Key
                             << "' is retired by the migration AND is still a reflected SceneSettings field, "
                                "so every run of the tool deletes a value somebody authored";
    }
}

// Every row has to say WHY, because the sentence is what reaches the person whose value disappeared. An
// empty reason is a removal nobody can account for a month later.
TEST( SceneRetiredKeysMigration, EveryRetiredRowNamesABlockAKeyAndAReason )
{
    ASSERT_GT( std::size( Migration::kRetiredKeys ), 0u );
    for ( const Migration::RetiredKey& row : Migration::kRetiredKeys )
    {
        EXPECT_TRUE( row.Block != nullptr && row.Block[0] != '\0' );
        EXPECT_TRUE( row.Key != nullptr && row.Key[0] != '\0' );
        EXPECT_GT( std::string( row.Why ).size(), 20u ) << "row '" << row.Key << "' has no usable reason";
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
