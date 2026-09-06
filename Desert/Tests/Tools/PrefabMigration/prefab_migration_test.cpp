// THE PREFAB MIGRATION, held to its one step and to the relation that makes the step safe to run.
//
// Tools/PrefabMigrator knows exactly one conversion: an UNVERSIONED .deprefab (the only generation that
// ever shipped — no prefab written before Д28 states any version) is stamped at the current heads and
// nothing else about it moves. This suite pins all four properties that sentence claims:
//
//   1. the stamp lands, and the outcome says so;
//   2. the entities are untouched — asserted by re-serializing them, not by trusting the code path;
//   3. a current file is left alone, and a foreign-generation file is refused rather than guessed at;
//   4. THE RELATION: what the migrator stamps is what the engine's gate requires — held structurally
//      (the migrator writes Core::kSceneVersion itself, no second constant) and asserted here through
//      the gate function the loader actually calls, so the two cannot drift even by re-spelling.
//
// PURE, like the migration: no filesystem, no GPU. The tool's write-back harness (temp file, re-parse
// through the engine gate, rename) is deliberately not driven from here — what it adds over the pure
// function is I/O, and the gate check it applies is the same PrefabIsAtCurrentVersion asserted below.

#include <PrefabMigration.hpp>

#include <Engine/Assets/Prefab/PrefabFormat.hpp>

#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <string>

using Desert::Assets::EntityData;
using Desert::Assets::PrefabData;
using Desert::Assets::PrefabIsAtCurrentVersion;
using Desert::Core::kSceneVersion;
using Desert::Core::kUnitVersion;
using Desert::Migration::MigratePrefab;

namespace
{
    // A pre-Д28 prefab: content, no version keys. The entities carry enough shape (ids, a parent, a
    // component payload) that "untouched" below means something.
    PrefabData LegacyPrefab()
    {
        PrefabData prefab;
        prefab.Name = "Legacy";

        EntityData root;
        root.id  = Common::UUID( 1001ull );
        root.Tag = "Root";
        prefab.Entities.push_back( root );

        EntityData child;
        child.id     = Common::UUID( 1002ull );
        child.parent = Common::UUID( 1001ull );
        child.Tag    = "Child";
        prefab.Entities.push_back( child );

        prefab.Root = Common::UUID( 1001ull );
        return prefab;
    }

    std::string EntitiesJson( const PrefabData& prefab )
    {
        return rfl::json::write( prefab.Entities );
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// 1 + 2. The one step: stamp lands, entities do not move
// ---------------------------------------------------------------------------------------------------

TEST( PrefabMigration, AnUnversionedPrefabIsStampedAtBothHeadsAndItsEntitiesDoNotMove )
{
    PrefabData        prefab = LegacyPrefab();
    const std::string before = EntitiesJson( prefab );

    const auto outcome = MigratePrefab( prefab );

    EXPECT_TRUE( outcome.Changed );
    EXPECT_FALSE( outcome.AlreadyCurrent );
    EXPECT_TRUE( outcome.Error.empty() ) << outcome.Error;
    EXPECT_EQ( outcome.FoundSceneVersion, 0 );
    EXPECT_EQ( outcome.FoundUnitVersion, 0 );

    ASSERT_TRUE( prefab.SceneVersion.has_value() );
    ASSERT_TRUE( prefab.UnitVersion.has_value() );
    EXPECT_EQ( *prefab.SceneVersion, kSceneVersion );
    EXPECT_EQ( *prefab.UnitVersion, kUnitVersion );

    // Byte-for-byte: the step is stamp-ONLY, and the report's claim to touch nothing else is measured.
    EXPECT_EQ( EntitiesJson( prefab ), before );
    EXPECT_EQ( prefab.Name, "Legacy" );
}

// ---------------------------------------------------------------------------------------------------
// 3. What is not migrated: the current file, and the foreign one
// ---------------------------------------------------------------------------------------------------

TEST( PrefabMigration, ACurrentPrefabIsReportedCurrentAndLeftAlone )
{
    PrefabData prefab        = LegacyPrefab();
    prefab.SceneVersion      = kSceneVersion;
    prefab.UnitVersion       = kUnitVersion;
    const std::string before = rfl::json::write( prefab );

    const auto outcome = MigratePrefab( prefab );

    EXPECT_FALSE( outcome.Changed );
    EXPECT_TRUE( outcome.AlreadyCurrent );
    EXPECT_TRUE( outcome.Error.empty() ) << outcome.Error;
    EXPECT_EQ( rfl::json::write( prefab ), before );
}

// A file stamped at any OTHER generation was written by a build this tool does not know — most likely a
// newer engine — and stamping over its number would be the silent substitution the gate forbids. The
// refusal names both numbers it found and both it knows, and the tree is untouched.
TEST( PrefabMigration, AForeignGenerationIsRefusedByItsOwnNumbersAndNotTouched )
{
    struct Case
    {
        int Scene;
        int Unit;
    };
    // Ahead on the schema axis; behind-but-stamped; stamped on one axis only (a half-write no build of
    // this engine produces — exactly why it must not be guessed at).
    const Case cases[] = {
         { kSceneVersion + 1, kUnitVersion }, { 1, kUnitVersion }, { kSceneVersion, 0 }, { 0, kUnitVersion } };

    for ( const Case& c : cases )
    {
        PrefabData prefab = LegacyPrefab();
        if ( c.Scene != 0 )
            prefab.SceneVersion = c.Scene;
        if ( c.Unit != 0 )
            prefab.UnitVersion = c.Unit;
        const std::string before = rfl::json::write( prefab );

        const auto outcome = MigratePrefab( prefab );

        EXPECT_FALSE( outcome.Changed ) << "v" << c.Scene << "/v" << c.Unit;
        EXPECT_FALSE( outcome.AlreadyCurrent ) << "v" << c.Scene << "/v" << c.Unit;
        ASSERT_FALSE( outcome.Error.empty() ) << "v" << c.Scene << "/v" << c.Unit << " was not refused";
        EXPECT_NE( outcome.Error.find( "v" + std::to_string( c.Scene ) ), std::string::npos ) << outcome.Error;
        EXPECT_NE( outcome.Error.find( "v" + std::to_string( c.Unit ) ), std::string::npos ) << outcome.Error;
        EXPECT_EQ( rfl::json::write( prefab ), before ) << "a refused tree was modified";
    }
}

// ---------------------------------------------------------------------------------------------------
// 4. THE RELATION: the version the migrator writes is the version the engine requires
// ---------------------------------------------------------------------------------------------------

// Asserted through the LOADER'S OWN gate function, not by comparing two spelled-out numbers: if the
// migrator ever stamps anything Core::kSceneVersion is not, every migrated prefab would be refused by
// the engine at once, and this is the test that says so before a user does. (SceneMigration pins the
// same relation with a static_assert on its last step constant; this tool derives the head instead of
// keeping a constant, and this test is what holds the derivation.)
TEST( PrefabMigration, WhatTheMigratorStampsTheEngineGateAccepts )
{
    PrefabData prefab = LegacyPrefab();
    ASSERT_FALSE( PrefabIsAtCurrentVersion( prefab ) ) << "the fixture must start below the gate";

    const auto outcome = MigratePrefab( prefab );

    ASSERT_TRUE( outcome.Changed );
    EXPECT_TRUE( PrefabIsAtCurrentVersion( prefab ) )
         << "MigratePrefab produced a tree the engine's own gate refuses - the migrator and the loader "
            "disagree about what the current generation is";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
