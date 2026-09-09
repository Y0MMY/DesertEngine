// THE `.anim` AND `.skeleton` FILE FORMATS ARE THESE STRUCTS, so this suite is a census of them.
//
// It exists because three fields named BoneIndex were declared without an initialiser and one of them —
// Serialization::ChannelData::BoneIndex — was written straight into every clip the Sequencer saved. The
// Sequencer's "New Clip" built its tracks without ever setting the index, so what reached the file was
// whatever the stack held, and the loader then sized its track array from `max(index) + 1`.
//
// The fix was not an initialiser. The index restated a bone's position in an array, which nothing at
// playback ever read (Animator::ResolveTrack binds by NAME), so the field is gone from the format. The two
// census tests below pin that: a field cannot come back without a line here saying so.

#include <Engine/Animation/BoneInfo.hpp>
#include <Engine/Assets/Serialization/Animation.hpp>
#include <Engine/Assets/Serialization/AnimationClipBuild.hpp>
#include <Engine/Assets/Serialization/Skeleton.hpp>

#include <Common/Core/Serialization/GlmReflection.hpp>

#include <rflcpp/rfl.hpp>
#include <rflcpp/rfl/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace Ser = Desert::Assets::Serialization;

namespace
{
    template <typename T>
    std::vector<std::string> FieldNames()
    {
        std::vector<std::string> names;
        for ( const auto& name : rfl::fields<T>() )
            names.push_back( std::string( name.name() ) );
        std::sort( names.begin(), names.end() );
        return names;
    }

    Ser::ChannelData Channel( const char* bone )
    {
        Ser::ChannelData ch;
        ch.BoneName = bone;
        ch.Positions.push_back( { 0.0f, glm::vec3( 1.0f, 2.0f, 3.0f ) } );
        return ch;
    }
} // namespace

// ============================================================================
// The census. NAMED ROWS, not a count: a count can be satisfied by editing the count.
// ============================================================================

TEST( AnimationClipFormat, ChannelFieldCensus )
{
    EXPECT_EQ( FieldNames<Ser::ChannelData>(),
               ( std::vector<std::string>{ "BoneName", "Positions", "Rotations", "Scales" } ) )
         << "a field added to or removed from the .anim channel changes every cooked clip on every machine";
}

TEST( AnimationClipFormat, AssetFieldCensus )
{
    EXPECT_EQ( FieldNames<Ser::AnimationAssetData>(),
               ( std::vector<std::string>{ "Channels", "Duration", "Name", "Notifies", "SkeletonSignature",
                                           "TicksPerSecond" } ) );
    EXPECT_EQ( FieldNames<Ser::NotifyData>(), ( std::vector<std::string>{ "Name", "Time" } ) );
}

TEST( AnimationClipFormat, SkeletonFieldCensus )
{
    EXPECT_EQ( FieldNames<Ser::SkeletonAssetData>(), ( std::vector<std::string>{ "Bones", "Signature" } ) );
    // BoneInfo is written to .skeleton verbatim; it carried the same redundant index.
    EXPECT_EQ( FieldNames<Desert::Animation::BoneInfo>(),
               ( std::vector<std::string>{ "LocalBindTransform", "Name", "OffsetMatrix", "ParentBoneID" } ) );
}

// Every scalar in the format has a default member initialiser, so a producer that forgets an assignment
// writes a defined value rather than heap noise. For these all-scalar structs the compiler can say so:
// without an initialiser they would be trivially default constructible, with one they are not.
static_assert( !std::is_trivially_default_constructible_v<Ser::KeyPosition> );
static_assert( !std::is_trivially_default_constructible_v<Ser::KeyRotation> );
static_assert( !std::is_trivially_default_constructible_v<Ser::KeyScale> );

// ============================================================================
// The behaviour the removed index used to control.
// ============================================================================

TEST( AnimationClipFormat, ChannelOrderIsPreservedAndBoundByName )
{
    Ser::AnimationAssetData data;
    data.Name              = "Walk";
    data.Duration          = 2.0f;
    data.TicksPerSecond    = 30.0f;
    data.SkeletonSignature = 1234u;
    data.Channels          = { Channel( "hips" ), Channel( "spine" ), Channel( "head" ) };

    const auto built = Desert::Assets::Serialization::BuildClipFromAssetData( data );
    ASSERT_TRUE( built ) << built.GetError();

    const auto& clip = built.GetValue();
    ASSERT_EQ( clip.Tracks.size(), 3u ) << "one track per channel: no index-shaped holes";
    EXPECT_EQ( clip.Tracks[0].BoneName, "hips" );
    EXPECT_EQ( clip.Tracks[1].BoneName, "spine" );
    EXPECT_EQ( clip.Tracks[2].BoneName, "head" );
    EXPECT_EQ( clip.AnimationName, "Walk" );
    EXPECT_FLOAT_EQ( clip.TicksPerSecond, 30.0f );
    EXPECT_EQ( clip.SkeletonSignature, 1234u );
}

TEST( AnimationClipFormat, AnUnnamedChannelIsRefusedByName )
{
    Ser::AnimationAssetData data;
    data.Name     = "Broken";
    data.Channels = { Channel( "hips" ), Channel( "" ) };

    const auto built = Desert::Assets::Serialization::BuildClipFromAssetData( data );
    ASSERT_FALSE( built ) << "an unnamed channel can never bind to a bone; accepting it is a silent loss";
    EXPECT_NE( built.GetError().find( "Broken" ), std::string::npos );
}

TEST( AnimationClipFormat, TwoChannelsForOneBoneAreRefused )
{
    Ser::AnimationAssetData data;
    data.Name     = "Doubled";
    data.Channels = { Channel( "hips" ), Channel( "hips" ) };

    const auto built = Desert::Assets::Serialization::BuildClipFromAssetData( data );
    ASSERT_FALSE( built ) << "playback resolves a bone to ONE track, so the loser's keys vanish";
    EXPECT_NE( built.GetError().find( "hips" ), std::string::npos );
}

TEST( AnimationClipFormat, NotifiesComeOutSortedByTime )
{
    Ser::AnimationAssetData data;
    data.Name     = "Notified";
    data.Notifies = { { "late", 0.9f }, { "early", 0.1f }, { "middle", 0.5f } };

    const auto built = Desert::Assets::Serialization::BuildClipFromAssetData( data );
    ASSERT_TRUE( built ) << built.GetError();
    ASSERT_EQ( built.GetValue().Notifies.size(), 3u );
    EXPECT_EQ( built.GetValue().Notifies[0].Name, "early" );
    EXPECT_EQ( built.GetValue().Notifies[2].Name, "late" );
}

// ============================================================================
// The on-disk migration, which is a migration BY OMISSION: reflect-cpp ignores fields the struct no longer
// declares, so a clip cooked before this change loads unchanged and re-cooks without the index. There is no
// version branch and no legacy path, which is the only reason that is acceptable — the test is what makes
// the claim checkable rather than believed.
// ============================================================================

TEST( AnimationClipFormat, AClipCookedWithTheOldBoneIndexStillLoads )
{
    const std::string legacy = R"({"Name":"Legacy","Duration":1.5,"TicksPerSecond":24.0,)"
                               R"("SkeletonSignature":77,"Channels":[)"
                               R"({"BoneName":"spine","BoneIndex":4,"Positions":[],"Rotations":[],"Scales":[]},)"
                               R"({"BoneName":"hips","BoneIndex":0,"Positions":[],"Rotations":[],"Scales":[]}])"
                               R"(,"Notifies":[]})";

    const auto read = rfl::json::read<Ser::AnimationAssetData, rfl::DefaultIfMissing>( legacy );
    ASSERT_TRUE( read.has_value() ) << "the retired field must be IGNORED, not rejected";

    const auto built = Desert::Assets::Serialization::BuildClipFromAssetData( read.value() );
    ASSERT_TRUE( built ) << built.GetError();

    const auto& clip = built.GetValue();
    ASSERT_EQ( clip.Tracks.size(), 2u )
         << "the old index said 4 and 0; the file's ORDER is what survives, and nothing is sized from it";
    EXPECT_EQ( clip.Tracks[0].BoneName, "spine" );
    EXPECT_EQ( clip.Tracks[1].BoneName, "hips" );
}

TEST( AnimationClipFormat, ANewlyWrittenClipCarriesNoBoneIndex )
{
    Ser::AnimationAssetData data;
    data.Name     = "Fresh";
    data.Channels = { Channel( "hips" ) };

    const std::string json = rfl::json::write( data );
    EXPECT_EQ( json.find( "BoneIndex" ), std::string::npos ) << json;
}

TEST( AnimationClipFormat, ASkeletonCookedWithTheOldBoneIndexStillLoads )
{
    const std::string legacy =
         R"({"Signature":4699069763035776985,"Bones":[{"BoneIndex":0,"Name":"Root",)"
         R"("OffsetMatrix":[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0],)"
         R"("LocalBindTransform":[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0]}]})";

    const auto read = rfl::json::read<Ser::SkeletonAssetData, rfl::DefaultIfMissing>( legacy );
    ASSERT_TRUE( read.has_value() );
    ASSERT_EQ( read.value().Bones.size(), 1u );
    EXPECT_EQ( read.value().Bones[0].Name, "Root" );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
