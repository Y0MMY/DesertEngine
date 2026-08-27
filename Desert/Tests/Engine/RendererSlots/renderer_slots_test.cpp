// Renderer slots are LEASED, and every lease must come back.
//
// There are six (Engine::kMaxRendererSlots), one per live view, and a slot is where that view's
// per-frame GPU state lives (Docs/RENDERER_FRAME_STATE.md). Past the sixth, a renderer records into
// slot 0 and shares the main viewport's camera, lights and shadow cascades. That failure has no error
// message: it looks like the Details preview moving when you move the scene camera, which is a picture
// defect discovered by a user, not a test.
//
// So the property under test is not "Claim returns a number". It is the RELATION between an open and a
// close: after any sequence of views being created and destroyed, the occupancy must be exactly the
// number still alive. A leak of one slot per opened-and-closed panel is invisible for five iterations
// and then corrupts the sixth, which is precisely the shape that reaches a user instead of a test.
//
// Nothing here touches Vulkan. That is the point: the accounting was three file-statics inside
// SceneRenderer.cpp, which no suite compiles and none can, so none of this was assertable at all.

#include <Engine/Core/RendererSlotPool.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using Desert::Engine::kMaxRendererSlots;
using Desert::Engine::RendererSlotLease;
using Desert::Engine::RendererSlotPool;

namespace
{
    // A view that owns a slot for as long as it exists — the same shape SceneRenderer has, and holding
    // the REAL lease type rather than a re-implementation of its rules, so this cannot pass while the
    // shipping type is wrong.
    using View = std::unique_ptr<RendererSlotLease>;

    View Open( RendererSlotPool& pool )
    {
        return std::make_unique<RendererSlotLease>( pool );
    }
} // namespace

TEST( RendererSlots, AFreshPoolIsEmpty )
{
    RendererSlotPool pool;
    EXPECT_EQ( pool.InUseCount(), 0u );
    EXPECT_FALSE( pool.IsFull() );
    EXPECT_FALSE( pool.IsInUse( 0 ) );
}

TEST( RendererSlots, ClaimTakesTheLowestFreeSlot )
{
    RendererSlotPool pool;
    EXPECT_EQ( pool.Claim(), 0u );
    EXPECT_EQ( pool.Claim(), 1u );
    EXPECT_EQ( pool.Claim(), 2u );
    EXPECT_EQ( pool.InUseCount(), 3u );
}

TEST( RendererSlots, AReleasedSlotIsHandedOutAgain )
{
    // The regression this whole file exists for. An earlier version only counted upwards, so opening and
    // closing scene views exhausted the range even though at most two were ever alive.
    RendererSlotPool pool;
    for ( uint32_t i = 0; i < kMaxRendererSlots * 4; ++i )
    {
        const uint32_t slot = pool.Claim();
        EXPECT_EQ( slot, 0u ) << "iteration " << i << ": a pool that is empty each time must hand out slot 0";
        pool.Release( slot );
        EXPECT_EQ( pool.InUseCount(), 0u );
    }
}

TEST( RendererSlots, OccupancyIsExactlyTheNumberOfLiveViews )
{
    RendererSlotPool  pool;
    std::vector<View> views;
    for ( uint32_t i = 0; i < kMaxRendererSlots; ++i )
    {
        views.push_back( Open( pool ) );
        EXPECT_EQ( pool.InUseCount(), i + 1 );
    }
    EXPECT_TRUE( pool.IsFull() );

    while ( !views.empty() )
    {
        views.pop_back();
        EXPECT_EQ( pool.InUseCount(), static_cast<uint32_t>( views.size() ) );
    }
    EXPECT_EQ( pool.InUseCount(), 0u );
}

TEST( RendererSlots, TwoLiveViewsNeverShareASlot )
{
    RendererSlotPool  pool;
    std::vector<View> views;
    std::vector<bool> seen( kMaxRendererSlots, false );
    for ( uint32_t i = 0; i < kMaxRendererSlots; ++i )
    {
        views.push_back( Open( pool ) );
        const uint32_t slot = views.back()->RecordingSlot();
        ASSERT_TRUE( views.back()->IsValid() );
        ASSERT_LT( slot, kMaxRendererSlots );
        EXPECT_FALSE( seen[slot] ) << "slot " << slot << " handed to two live views at once";
        seen[slot] = true;
    }
}

TEST( RendererSlots, AnOverflowingViewRecordsIntoSlotZeroAndSaysSo )
{
    RendererSlotPool  pool;
    std::vector<View> views;
    for ( uint32_t i = 0; i < kMaxRendererSlots; ++i )
        views.push_back( Open( pool ) );

    const View overflow = Open( pool );
    EXPECT_FALSE( overflow->IsValid() );
    EXPECT_EQ( overflow->RecordingSlot(), 0u );
    // It took nothing, so the pool is unchanged: still exactly the six that are really held.
    EXPECT_EQ( pool.InUseCount(), kMaxRendererSlots );
}

TEST( RendererSlots, AnOverflowingViewDoesNotGiveAwaySlotZeroWhenItDies )
{
    // The subtle one, and a real defect in the code this replaced: the old claim returned 0 on overflow
    // WITHOUT taking it, and the old destructor released whatever number it was holding. So an
    // overflowing renderer's destructor freed the MAIN VIEWPORT's lease, after which the next renderer
    // created was handed slot 0 as if it were free — two live views recording into one slot, while the
    // mask insisted only one was taken. It needed a seventh renderer in a session to appear, which is
    // exactly why nobody hit it until the slots got tight.
    RendererSlotPool  pool;
    std::vector<View> views;
    for ( uint32_t i = 0; i < kMaxRendererSlots; ++i )
        views.push_back( Open( pool ) );

    const uint32_t mainViewportSlot = views.front()->RecordingSlot();
    {
        const View overflow = Open( pool );
        EXPECT_FALSE( overflow->IsValid() );
    } // dies here

    EXPECT_TRUE( pool.IsInUse( mainViewportSlot ) ) << "the overflowing view released a lease it never took";
    EXPECT_EQ( pool.InUseCount(), kMaxRendererSlots );
}

// --- The editor's own surfaces ---------------------------------------------------------------------
//
// The six slots are spent by: the main viewport, each extra scene view, the Details preview, the
// material editor's preview, the asset thumbnail renderer and the photogrammetry preview. The main
// viewport is permanent; every other one is opened and closed by the user, repeatedly, in any order.

TEST( RendererSlots, EveryPreviewSurfaceOpenedAndClosedReturnsToTheBaseline )
{
    // The measurement the fix is judged by, as a test: open each transient surface in turn, close it, and
    // require the occupancy to be back to the baseline every time. A surface that returns nothing shows
    // up here on its FIRST cycle rather than on the user's sixth.
    RendererSlotPool pool;

    const View     mainViewport = Open( pool ); // never closed while the editor runs
    const uint32_t baseline     = pool.InUseCount();
    ASSERT_EQ( baseline, 1u );

    constexpr int kTransientSurfaces = 5; // everything above except the main viewport
    for ( int cycle = 0; cycle < 4; ++cycle )
    {
        for ( int surface = 0; surface < kTransientSurfaces; ++surface )
        {
            const View opened = Open( pool );
            EXPECT_TRUE( opened->IsValid() ) << "cycle " << cycle << ", surface " << surface
                                             << ": ran out of slots, so an earlier one was never returned";
            EXPECT_EQ( pool.InUseCount(), baseline + 1 );
        }
        EXPECT_EQ( pool.InUseCount(), baseline )
             << "cycle " << cycle << ": occupancy did not return to the baseline after closing every surface";
    }

    EXPECT_TRUE( mainViewport->IsValid() );
    EXPECT_EQ( pool.InUseCount(), baseline );
}

TEST( RendererSlots, SurfacesOpenAtOnceStillFitAndStillAllFree )
{
    // The tight case the material editor window made real: the main viewport plus every preview surface
    // alive simultaneously. Five transient + one permanent is exactly six, so this must fit with no
    // sharing — and must drain completely.
    RendererSlotPool pool;

    std::vector<View> views;
    views.push_back( Open( pool ) ); // main viewport
    for ( int i = 0; i < 5; ++i )
        views.push_back( Open( pool ) );

    for ( const auto& view : views )
        EXPECT_TRUE( view->IsValid() ) << "the editor's own surfaces do not fit in " << kMaxRendererSlots;
    EXPECT_TRUE( pool.IsFull() );

    views.clear();
    EXPECT_EQ( pool.InUseCount(), 0u );
}

TEST( RendererSlots, ClosingOutOfOrderStillReturnsEverything )
{
    // Panels are closed in whatever order the user closes them, not the order they were opened.
    RendererSlotPool  pool;
    std::vector<View> views;
    for ( uint32_t i = 0; i < kMaxRendererSlots; ++i )
        views.push_back( Open( pool ) );

    views.erase( views.begin() + 2 );
    EXPECT_EQ( pool.InUseCount(), kMaxRendererSlots - 1 );

    views.erase( views.begin() );
    EXPECT_EQ( pool.InUseCount(), kMaxRendererSlots - 2 );

    // The two freed slots are available again, and to different views.
    const View reopenedA = Open( pool );
    const View reopenedB = Open( pool );
    ASSERT_TRUE( reopenedA->IsValid() );
    ASSERT_TRUE( reopenedB->IsValid() );
    EXPECT_NE( reopenedA->RecordingSlot(), reopenedB->RecordingSlot() );
    EXPECT_EQ( pool.InUseCount(), kMaxRendererSlots );
}

// Only gtest is linked, not gtest_main — every suite in this tree brings its own entry point.
int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
