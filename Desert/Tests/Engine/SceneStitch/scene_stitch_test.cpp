// The identity stitch every scene load in this engine goes through.
//
// WHY THIS SUITE EXISTS AT ALL. Until it did, nothing in the repository compiled
// Engine/Core/Serialize/SceneSerializer.cpp - it includes Scene.hpp, Scene.hpp reaches the renderer, and no
// test project links that. So the loop that decides which entity a saved record becomes, which entity its
// components are written onto and what it is parented to could be broken deliberately and all 69 suites
// still passed. The logic now lives in a pure function (Rules::PlanSceneStitch) and this is the thing that
// reaches it.
//
// WHAT IS ASSERTED, and why each one is a defect that has actually happened or can:
//
//   1. An id written in the file is the id the entity gets, and an entity with no id is minted ONE id.
//      The two passes used to mint independently and disagree, and the entity came out bare.
//   2. Parent links resolve to the entity that carries the named id, in the file's own order.
//   3. A parent that is not in the file leaves the child unattached and is COUNTED, not swallowed.
//   4. A parent id of 0 is "no parent", not "the entity whose id is 0".
//   5. A prefab-root record is listed, not stitched, and does not answer parent links in pass 2 - which is
//      what the loader does, and is the difference between a documented limitation and a surprise.
//   6. Two records claiming one id load as the map makes them load (first claimant wins) and are counted.
//   7. The relation between the three lists: every record is either created or a prefab root, exactly once.
//
// No Scene, no renderer, no filesystem: the whole point of the split.

#include <Engine/Core/Serialize/SceneStitchRules.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using Desert::Assets::EntityData;
using Desert::Core::Rules::kNoSlot;
using Desert::Core::Rules::PlanSceneStitch;
using Desert::Core::Rules::StitchPlan;

namespace
{
    // A mint that is reproducible, so a test can name the id it expects. Ids start high enough that they
    // cannot be confused with the authored ones the tests write by hand.
    class CountingMint
    {
    public:
        Common::UUID operator()()
        {
            ++m_Calls;
            return Common::UUID( m_Next++ );
        }

        int Calls() const
        {
            return m_Calls;
        }

    private:
        uint64_t m_Next  = 900000;
        int      m_Calls = 0;
    };

    EntityData Plain( uint64_t id, const char* tag )
    {
        EntityData d;
        d.id  = Common::UUID( id );
        d.Tag = std::string( tag );
        return d;
    }

    EntityData Child( uint64_t id, uint64_t parent, const char* tag )
    {
        EntityData d = Plain( id, tag );
        d.parent     = Common::UUID( parent );
        return d;
    }

    EntityData Anonymous( const char* tag )
    {
        EntityData d;
        d.Tag = std::string( tag );
        return d;
    }

    EntityData PrefabRoot( uint64_t id, const char* file )
    {
        EntityData d = Plain( id, "PrefabRoot" );
        d.PrefabPath = std::string( file );
        return d;
    }
} // namespace

// 1a. The id in the file is the id of the entity, and it is decided once.
TEST( SceneStitch, AuthoredIdIsKept )
{
    const std::vector<EntityData> records = { Plain( 11, "A" ), Plain( 22, "B" ) };
    CountingMint                  mint;

    const StitchPlan plan = PlanSceneStitch( records, mint );

    ASSERT_EQ( plan.Created.size(), 2u );
    EXPECT_EQ( (uint64_t)plan.Created[0].Id, 11u );
    EXPECT_EQ( (uint64_t)plan.Created[1].Id, 22u );
    EXPECT_FALSE( plan.Created[0].IdMinted );
    EXPECT_FALSE( plan.Created[1].IdMinted );
    EXPECT_EQ( mint.Calls(), 0 );
    EXPECT_EQ( plan.Minted, 0u );
}

// 1b. THE REGRESSION. A record with no id is minted exactly ONE id, and pass 2 lands its payload on the
// entity that id created. The old loader minted in both passes, so pass 2 looked the entity up under an id
// nothing had been created with, missed, and skipped the record entirely: the entity survived the load
// carrying nothing but its tag.
TEST( SceneStitch, RecordWithoutIdIsMintedOnceAndStillReceivesItsPayload )
{
    const std::vector<EntityData> records = { Anonymous( "NoId" ) };
    CountingMint                  mint;

    const StitchPlan plan = PlanSceneStitch( records, mint );

    ASSERT_EQ( plan.Created.size(), 1u );
    EXPECT_TRUE( plan.Created[0].IdMinted );
    EXPECT_EQ( mint.Calls(), 1 );
    EXPECT_EQ( plan.Minted, 1u );

    ASSERT_EQ( plan.Loads.size(), 1u );
    EXPECT_EQ( plan.Loads[0].Record, 0u );
    EXPECT_EQ( plan.Loads[0].Target, 0u ); // the entity that was actually created, not a second mint
}

// 1c. One mint per idless record, however many there are - and never for a record that names an id.
TEST( SceneStitch, MintIsCalledOncePerIdlessRecord )
{
    const std::vector<EntityData> records = { Anonymous( "a" ), Plain( 7, "b" ), Anonymous( "c" ),
                                              Anonymous( "d" ) };
    CountingMint                  mint;

    const StitchPlan plan = PlanSceneStitch( records, mint );

    EXPECT_EQ( mint.Calls(), 3 );
    EXPECT_EQ( plan.Minted, 3u );
    EXPECT_NE( (uint64_t)plan.Created[0].Id, (uint64_t)plan.Created[2].Id );
    EXPECT_NE( (uint64_t)plan.Created[2].Id, (uint64_t)plan.Created[3].Id );
}

// 2. A parent link names an id; it resolves to the slot of the entity created under it - forwards...
TEST( SceneStitch, ParentLinkResolvesToTheNamedEntity )
{
    const std::vector<EntityData> records = { Plain( 11, "Parent" ), Child( 22, 11, "Child" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 2u );
    EXPECT_EQ( plan.Loads[0].Parent, kNoSlot );
    EXPECT_EQ( plan.Loads[1].Parent, 0u );
    EXPECT_EQ( plan.UnresolvedParents, 0u );
}

// ...and backwards. Pass 1 creates every entity before pass 2 links any, so a child written ABOVE its
// parent in the file still finds it. A single-pass loader would not, and this is why there are two.
TEST( SceneStitch, ParentLinkResolvesWhenTheParentComesLaterInTheFile )
{
    const std::vector<EntityData> records = { Child( 22, 11, "Child" ), Plain( 11, "Parent" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 2u );
    EXPECT_EQ( plan.Loads[0].Parent, 1u );
    EXPECT_EQ( plan.Loads[1].Parent, kNoSlot );
}

// 3. A parent nothing answers to leaves the child at the root, and is COUNTED. Silently attaching it to
// whatever the map happened to return would move geometry; silently dropping it would lose a hierarchy
// nobody is told about.
TEST( SceneStitch, ParentThatIsNotInTheFileIsCountedAndLeavesTheChildUnattached )
{
    const std::vector<EntityData> records = { Child( 22, 999, "Orphan" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 1u );
    EXPECT_EQ( plan.Loads[0].Parent, kNoSlot );
    EXPECT_EQ( plan.UnresolvedParents, 1u );
}

// 4. A null (0) parent is the spelling of "no parent" - see Common/Core/UUID.hpp on why 0 is the safe
// value. It must not be looked up, or an entity that genuinely carries id 0 becomes everybody's parent.
TEST( SceneStitch, NullParentIsNoParentAndIsNotCountedAsUnresolved )
{
    std::vector<EntityData> records = { Plain( 0, "IdZero" ), Child( 22, 0, "Loose" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 2u );
    EXPECT_EQ( plan.Loads[1].Parent, kNoSlot );
    EXPECT_EQ( plan.UnresolvedParents, 0u );
}

// 5a. A prefab root is LISTED for pass 3, never created here: it comes out of another file and the record
// alone cannot make it.
TEST( SceneStitch, PrefabRootsAreListedAndNotCreated )
{
    const std::vector<EntityData> records = { Plain( 11, "A" ), PrefabRoot( 33, "Prefabs/Tree.deprefab" ),
                                              Plain( 22, "B" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.PrefabRecords.size(), 1u );
    EXPECT_EQ( plan.PrefabRecords[0], 1u );
    ASSERT_EQ( plan.Created.size(), 2u );
    EXPECT_EQ( plan.Created[0].Record, 0u );
    EXPECT_EQ( plan.Created[1].Record, 2u );
}

// 5b. And it does not answer a plain entity's parent link, because the loader registers prefab roots only
// AFTER pass 2 has run. Pinned deliberately: it is the engine's behaviour today, so a change to it should
// break a test rather than quietly re-parent somebody's scene.
TEST( SceneStitch, PlainEntityParentedToAPrefabRootDoesNotResolveInPassTwo )
{
    const std::vector<EntityData> records = { PrefabRoot( 33, "Prefabs/Tree.deprefab" ),
                                              Child( 22, 33, "HangsOffThePrefab" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 1u );
    EXPECT_EQ( plan.Loads[0].Parent, kNoSlot );
    EXPECT_EQ( plan.UnresolvedParents, 1u );
}

// 6. Two records claiming one id. The loader registers with map::insert, so the FIRST keeps the id and the
// second record's payload is written onto the first record's entity - the second entity stays bare. Pinned
// as-is and counted, because changing what a corrupt file loads as is a decision about scene data.
TEST( SceneStitch, DuplicateIdLoadsOntoTheFirstClaimantAndIsCounted )
{
    const std::vector<EntityData> records = { Plain( 11, "First" ), Plain( 11, "Second" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Created.size(), 2u ); // both entities are still created
    ASSERT_EQ( plan.Loads.size(), 2u );
    EXPECT_EQ( plan.Loads[0].Target, 0u );
    EXPECT_EQ( plan.Loads[1].Target, 0u ); // record 1's payload lands on record 0's entity
    EXPECT_EQ( plan.Shadowed, 1u );
}

// A child of a duplicated id attaches to the claimant, matching the map the loader builds.
TEST( SceneStitch, ChildOfADuplicatedIdAttachesToTheClaimant )
{
    const std::vector<EntityData> records = { Plain( 11, "First" ), Plain( 11, "Second" ),
                                              Child( 22, 11, "Child" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    ASSERT_EQ( plan.Loads.size(), 3u );
    EXPECT_EQ( plan.Loads[2].Parent, 0u );
}

// 7. The relation, on a tree with one of everything: every record is accounted for exactly once, the two
// pass lists line up, and every slot a Load names exists.
TEST( SceneStitch, EveryRecordIsAccountedForExactlyOnce )
{
    const std::vector<EntityData> records = { Plain( 11, "A" ), Anonymous( "B" ),
                                              PrefabRoot( 33, "Prefabs/Tree.deprefab" ), Child( 22, 11, "C" ),
                                              PrefabRoot( 44, "Prefabs/Rock.deprefab" ) };

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    EXPECT_EQ( plan.Created.size() + plan.PrefabRecords.size(), records.size() );
    EXPECT_EQ( plan.Loads.size(), plan.Created.size() );

    std::vector<int> seen( records.size(), 0 );
    for ( const auto& created : plan.Created )
        ++seen[created.Record];
    for ( const size_t record : plan.PrefabRecords )
        ++seen[record];
    for ( const int count : seen )
        EXPECT_EQ( count, 1 );

    for ( size_t slot = 0; slot < plan.Loads.size(); ++slot )
    {
        EXPECT_EQ( plan.Loads[slot].Record, plan.Created[slot].Record );
        EXPECT_LT( plan.Loads[slot].Target, plan.Created.size() );
        EXPECT_TRUE( plan.Loads[slot].Parent == kNoSlot || plan.Loads[slot].Parent < plan.Created.size() );
    }
}

// An empty file is a scene with nothing in it, not a crash and not a phantom entity.
TEST( SceneStitch, EmptyTreePlansNothing )
{
    const std::vector<EntityData> records;

    const StitchPlan plan = PlanSceneStitch( records, CountingMint() );

    EXPECT_TRUE( plan.Created.empty() );
    EXPECT_TRUE( plan.Loads.empty() );
    EXPECT_TRUE( plan.PrefabRecords.empty() );
    EXPECT_EQ( plan.Minted, 0u );
    EXPECT_EQ( plan.Shadowed, 0u );
    EXPECT_EQ( plan.UnresolvedParents, 0u );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
