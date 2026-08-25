// Where GPU timestamps live in the query pool, and whether a breakdown built from them adds up.
//
// Two relations, both of which have already been wrong in this engine:
//
//   1. A query belongs to exactly one (frame x renderer slot). This is Docs/RENDERER_FRAME_STATE.md's
//      rule applied to a new per-frame GPU resource. The editor runs several live SceneRenderers into a
//      single command buffer, so a pool keyed by frame alone would have the asset preview's
//      "VolumetricClouds" overwrite the viewport's — the exact shape of the bug that document exists
//      because of, and one that shows up as a plausible wrong number rather than an error.
//
//   2. Self times partition the root. Passes nest, so summing the INCLUSIVE column counts a parent's
//      microseconds again in every child: the first breakdown this feature ever printed came to 159 % of
//      its own frame. Subtracting direct children makes the remainder a partition.
//
// Nothing here touches Vulkan.

#include <Engine/Graphic/GpuTimestampLayout.hpp>

#include <gtest/gtest.h>

#include <set>
#include <vector>

using Desert::Graphic::GpuDecodeFrame;
using Desert::Graphic::GpuDecodeSlot;
using Desert::Graphic::GpuFrameTotalQueryBase;
using Desert::Graphic::GpuQueriesPerFrame;
using Desert::Graphic::GpuQueriesPerSlot;
using Desert::Graphic::GpuSelfTimes;
using Desert::Graphic::GpuSlotQueryBase;
using Desert::Graphic::kGpuNoParent;

namespace
{
    // The shipping shape: kMaxRendererSlots = 6, kMaxScopesPerFrameSlot = 64, 3 frames in flight on this
    // swapchain. Deliberately not read from the engine headers — a test that follows the constant it is
    // checking cannot notice the constant changing underneath it.
    constexpr uint32_t kSlots  = 6;
    constexpr uint32_t kScopes = 64;
    constexpr uint32_t kFrames = 3;
} // namespace

// --- 1. the (frame x slot) partition -----------------------------------------------------------------

// THE relation. Every query index reachable by any (frame, slot, scope) must be reachable by exactly one
// of them, or two renderers share a timestamp and the numbers silently belong to the wrong view.
TEST( GpuTimestampLayout, EveryQueryBelongsToExactlyOneFrameAndSlot )
{
    std::set<uint32_t> seen;

    for ( uint32_t frame = 0; frame < kFrames; ++frame )
    {
        for ( uint32_t slot = 0; slot < kSlots; ++slot )
        {
            for ( uint32_t scope = 0; scope < kScopes; ++scope )
            {
                const uint32_t begin = GpuSlotQueryBase( frame, slot, kSlots, kScopes ) + scope * 2;
                EXPECT_TRUE( seen.insert( begin ).second )
                     << "begin query " << begin << " is claimed twice (frame " << frame << ", slot " << slot
                     << ", scope " << scope << ")";
                EXPECT_TRUE( seen.insert( begin + 1 ).second ) << "end query " << begin + 1 << " is claimed twice";
            }
        }

        const uint32_t total = GpuFrameTotalQueryBase( frame, kSlots, kScopes );
        EXPECT_TRUE( seen.insert( total ).second ) << "frame-total begin collides with a slot's queries";
        EXPECT_TRUE( seen.insert( total + 1 ).second ) << "frame-total end collides";
    }

    // And nothing was allocated that the pool does not cover.
    EXPECT_EQ( seen.size(), static_cast<size_t>( kFrames ) * GpuQueriesPerFrame( kSlots, kScopes ) );
    EXPECT_LT( *seen.rbegin(), kFrames * GpuQueriesPerFrame( kSlots, kScopes ) );
}

// A scope closes by decoding its own handle, so the decode must invert the encode for every scope a slot
// can hold. If it does not, EndScope pops the wrong slot's nesting stack and the self times go with it.
TEST( GpuTimestampLayout, DecodeInvertsEncode )
{
    for ( uint32_t frame = 0; frame < kFrames; ++frame )
        for ( uint32_t slot = 0; slot < kSlots; ++slot )
            for ( uint32_t scope = 0; scope < kScopes; ++scope )
            {
                const uint32_t query = GpuSlotQueryBase( frame, slot, kSlots, kScopes ) + scope * 2;
                EXPECT_EQ( GpuDecodeFrame( query, kSlots, kScopes ), frame );
                EXPECT_EQ( GpuDecodeSlot( query, kSlots, kScopes ), slot );
                // The END query decodes to the same owner as its begin — they are one scope.
                EXPECT_EQ( GpuDecodeFrame( query + 1, kSlots, kScopes ), frame );
                EXPECT_EQ( GpuDecodeSlot( query + 1, kSlots, kScopes ), slot );
            }
}

// The frame bracket belongs to no renderer. Decoding it as slot 0's would let the whole-frame time be
// mistaken for a pass of the main viewport — which is precisely the row a breakdown must not sum.
TEST( GpuTimestampLayout, FrameTotalDecodesOutsideEverySlot )
{
    for ( uint32_t frame = 0; frame < kFrames; ++frame )
    {
        const uint32_t total = GpuFrameTotalQueryBase( frame, kSlots, kScopes );
        EXPECT_EQ( GpuDecodeFrame( total, kSlots, kScopes ), frame );
        EXPECT_EQ( GpuDecodeSlot( total, kSlots, kScopes ), kSlots ) << "must be out of slot range";
    }
}

// A slot's block must not run into the next slot's, whatever the scope budget is.
TEST( GpuTimestampLayout, SlotBlocksAreContiguousAndDoNotOverlap )
{
    for ( uint32_t slot = 0; slot + 1 < kSlots; ++slot )
    {
        const uint32_t here = GpuSlotQueryBase( 0, slot, kSlots, kScopes );
        const uint32_t next = GpuSlotQueryBase( 0, slot + 1, kSlots, kScopes );
        EXPECT_EQ( next - here, GpuQueriesPerSlot( kScopes ) );
        // The last query this slot can write stays below the next slot's first.
        EXPECT_LT( here + ( kScopes - 1 ) * 2 + 1, next );
    }
}

// --- 2. self times partition the root ----------------------------------------------------------------

namespace
{
    double Sum( const std::vector<double>& v )
    {
        double total = 0.0;
        for ( double x : v )
            total += x;
        return total;
    }
} // namespace

// The frame's real shape: VolumetricClouds > Clouds: ExecuteInFrame > {March, TemporalResolve}. Summing
// the inclusive column here gives 8.0 + 7.9 + 7.1 + 0.7 = 23.7 ms out of an 8.0 ms pass; the self times
// give back exactly 8.0.
TEST( GpuTimestampLayout, SelfTimesSumToTheRootInclusive )
{
    //                       0: VolumetricClouds  1: ExecuteInFrame  2: March  3: Resolve
    const std::vector<double>  inclusive{ 8.000, 7.900, 7.100, 0.700 };
    const std::vector<int32_t> parents{ kGpuNoParent, 0, 1, 1 };

    const std::vector<double> self = GpuSelfTimes( inclusive, parents );

    EXPECT_DOUBLE_EQ( self[0], 0.100 );   // 8.000 - 7.900
    EXPECT_NEAR( self[1], 0.100, 1e-12 ); // 7.900 - 7.100 - 0.700
    EXPECT_DOUBLE_EQ( self[2], 7.100 );   // leaves keep their own time
    EXPECT_DOUBLE_EQ( self[3], 0.700 );
    EXPECT_NEAR( Sum( self ), inclusive[0], 1e-12 ); // the partition
}

// Siblings at the top level are not each other's children, so the total is their sum — this is what makes
// the frame's own bracket comparable with the passes under it.
TEST( GpuTimestampLayout, IndependentRootsAreNotSubtractedFromEachOther )
{
    const std::vector<double>  inclusive{ 4.7, 7.1, 0.6 };
    const std::vector<int32_t> parents{ kGpuNoParent, kGpuNoParent, kGpuNoParent };

    const std::vector<double> self = GpuSelfTimes( inclusive, parents );

    EXPECT_DOUBLE_EQ( self[0], 4.7 );
    EXPECT_DOUBLE_EQ( self[1], 7.1 );
    EXPECT_DOUBLE_EQ( self[2], 0.6 );
    EXPECT_NEAR( Sum( self ), 12.4, 1e-12 );
}

// A scope whose two queries did not both land is marked -1 and must not be charged to its parent: doing
// so would inflate a sibling's apparent cost by a pass that was never measured.
TEST( GpuTimestampLayout, UnresolvedScopesDoNotDisturbTheirParent )
{
    const std::vector<double>  inclusive{ 5.0, -1.0, 2.0 };
    const std::vector<int32_t> parents{ kGpuNoParent, 0, 0 };

    const std::vector<double> self = GpuSelfTimes( inclusive, parents );

    EXPECT_DOUBLE_EQ( self[0], 3.0 ); // 5.0 - 2.0, the -1 child ignored
    EXPECT_LT( self[1], 0.0 );        // stays marked unresolved
    EXPECT_DOUBLE_EQ( self[2], 2.0 );
}

// A child measured slightly longer than its parent (the two timestamps are separate writes, so this is
// possible at the microsecond level) must clamp to zero rather than go negative — a negative self time
// would quietly shrink the total and read as unattributed GPU work that does not exist.
TEST( GpuTimestampLayout, SelfTimeNeverGoesNegative )
{
    const std::vector<double>  inclusive{ 1.000, 1.002 };
    const std::vector<int32_t> parents{ kGpuNoParent, 0 };

    const std::vector<double> self = GpuSelfTimes( inclusive, parents );

    EXPECT_DOUBLE_EQ( self[0], 0.0 );
    EXPECT_DOUBLE_EQ( self[1], 1.002 );
}

// Deep nesting: the engine really does reach four levels (frame > SceneRenderer::OnUpdate >
// VolumetricClouds > Clouds: ExecuteInFrame > Clouds: March), and only DIRECT children may be
// subtracted — subtracting grandchildren twice was the other way to get this wrong.
TEST( GpuTimestampLayout, OnlyDirectChildrenAreSubtracted )
{
    const std::vector<double>  inclusive{ 10.0, 9.0, 8.0, 7.0 };
    const std::vector<int32_t> parents{ kGpuNoParent, 0, 1, 2 };

    const std::vector<double> self = GpuSelfTimes( inclusive, parents );

    EXPECT_DOUBLE_EQ( self[0], 1.0 );
    EXPECT_DOUBLE_EQ( self[1], 1.0 );
    EXPECT_DOUBLE_EQ( self[2], 1.0 );
    EXPECT_DOUBLE_EQ( self[3], 7.0 );
    EXPECT_NEAR( Sum( self ), inclusive[0], 1e-12 );
}
