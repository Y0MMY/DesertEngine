// THE RELATION THIS GUARDS, stated once and then asserted as a property rather than as a table of
// answers:
//
//     for EVERY state a cached thumbnail can be observed in, the editor either SHOWS the cached
//     picture or SCHEDULES a new one. Never neither.
//
// "Never neither" is the whole point, and it is the half no per-side unit test can see. Both sides of
// the thumbnail system were individually correct before this header existed:
//
//   * the readers (asset grid, preview pane, Details material slot) drew the PNG only when it existed
//     AND the asset was not meaningfully newer than it;
//   * ThumbnailService::ShouldQueue refused to queue whenever the PNG merely EXISTED.
//
// Their intersection is a hole: PNG present, source newer than it by more than the margin. Every
// reader calls that unusable and the queue calls it done, so nothing is drawn and nothing is
// rendered -- for the rest of the session and every session after it, because neither side changes
// either fact. Measured in the editor: `touch`ing one `.demat` whose thumbnail was on disk left that
// material showing a flat colour swatch across a restart, with no capture logged and no PNG rewritten.
//
// WHAT THIS FILE DELIBERATELY DOES NOT ASSERT. It does not re-derive the three-second margin from the
// filesystem's rounding, and it does not pin which side of the boundary a difference of exactly three
// seconds falls on beyond one explicit case -- a test that spelled the arithmetic out a second time
// would fail when the margin is retuned while every property that matters still holds. What it pins is
// the shape: totality, the direction of the two monotonicities, and the two states that were the
// defect.

#include <gtest/gtest.h>

#include <Editor/Widgets/ThumbnailFreshness.hpp>

#include <chrono>
#include <vector>

namespace
{
    namespace TF = Desert::Editor::ThumbnailFreshness;

    using Seconds = std::chrono::seconds;

    TF::Observation Seen( bool pngExists, bool stampsReadable, long long sourceNewerBySeconds )
    {
        TF::Observation seen;
        seen.PngExists      = pngExists;
        seen.StampsReadable = stampsReadable;
        seen.SourceNewerBy  = Seconds( sourceNewerBySeconds );
        return seen;
    }

    // Every observation the filesystem can produce, at a spread of offsets that straddles the margin in
    // both directions and includes the two degenerate stamps (equal, and the source OLDER, which is the
    // normal case for an asset that has not been touched since its capture).
    std::vector<TF::Observation> EveryState()
    {
        std::vector<TF::Observation> all;
        for ( const bool pngExists : { false, true } )
        {
            for ( const bool readable : { false, true } )
            {
                for ( const long long offset :
                      { -100000LL, -10LL, -1LL, 0LL, 1LL, 2LL, 3LL, 4LL, 10LL, 100000LL } )
                {
                    all.push_back( Seen( pngExists, readable, offset ) );
                }
            }
        }
        return all;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The property: totality. Judge answers, always, and the answer is one of the two.
// ---------------------------------------------------------------------------------------------------

TEST( ThumbnailFreshness, EveryObservationHasExactlyOneVerdict )
{
    for ( const TF::Observation& seen : EveryState() )
    {
        const TF::Verdict verdict = TF::Judge( seen );

        // Exhaustive by construction in C++, and asserted anyway: the value that reaches a switch in a
        // panel must be one of the two the panel handles, and an enum can be given a third name by the
        // next person to touch the header. The hole this whole file is about was created exactly that
        // way -- by a third state nobody wrote down.
        const bool shows    = ( verdict == TF::Verdict::Show );
        const bool captures = ( verdict == TF::Verdict::Capture );
        EXPECT_TRUE( shows || captures )
             << "no verdict for pngExists=" << seen.PngExists << " readable=" << seen.StampsReadable
             << " newerBy=" << seen.SourceNewerBy.count();
        EXPECT_FALSE( shows && captures );
    }
}

// ---------------------------------------------------------------------------------------------------
// The two states that WERE the defect. Both sides asked about one observation and disagreed; here they
// ask the one function, so they cannot.
// ---------------------------------------------------------------------------------------------------

TEST( ThumbnailFreshness, AStaleThumbnailIsScheduled_NotSilentlyKept )
{
    // The hole: the PNG is there (so the old ShouldQueue said "cached, do nothing") and the source is
    // well past the margin (so every reader said "do not show this"). Nothing drew and nothing rendered.
    EXPECT_EQ( TF::Judge( Seen( /*pngExists=*/true, /*readable=*/true, /*newerBy=*/3600 ) ),
               TF::Verdict::Capture );
}

TEST( ThumbnailFreshness, AMissingThumbnailIsScheduled )
{
    // The other side of the same statement: with no PNG at all the stamps carry no information, so the
    // verdict must not depend on them.
    for ( const bool readable : { false, true } )
    {
        for ( const long long offset : { -10LL, 0LL, 10LL } )
        {
            EXPECT_EQ( TF::Judge( Seen( /*pngExists=*/false, readable, offset ) ), TF::Verdict::Capture );
        }
    }
}

TEST( ThumbnailFreshness, AFreshThumbnailIsShownAndNotRecaptured )
{
    // The owner's complaint in one assertion: a cached picture whose asset has not moved is DISPLAYED,
    // not computed again. The normal case is the source being older than its own thumbnail.
    EXPECT_EQ( TF::Judge( Seen( true, true, -100000 ) ), TF::Verdict::Show );
    EXPECT_EQ( TF::Judge( Seen( true, true, 0 ) ), TF::Verdict::Show );
}

// ---------------------------------------------------------------------------------------------------
// The margin, as a MONOTONICITY rather than as a number. "More stale never becomes more usable" catches
// an inverted comparison and an off-by-one at the boundary, which a spot value does not.
// ---------------------------------------------------------------------------------------------------

TEST( ThumbnailFreshness, StalenessIsMonotone )
{
    // Walking the source from far older to far newer, the verdict may flip Show -> Capture ONCE and
    // never back. An inverted comparison fails this on the first step; an off-by-one at the boundary
    // fails the two explicit cases below.
    bool captured = false;
    for ( long long offset = -20; offset <= 20; ++offset )
    {
        const TF::Verdict verdict = TF::Judge( Seen( true, true, offset ) );
        if ( captured )
        {
            EXPECT_EQ( verdict, TF::Verdict::Capture )
                 << "a staler thumbnail became usable again at offset " << offset;
        }
        if ( verdict == TF::Verdict::Capture )
        {
            EXPECT_GT( offset, 0 ) << "a thumbnail NEWER than its asset was called stale at offset " << offset;
        }
        captured = captured || ( verdict == TF::Verdict::Capture );
    }
    EXPECT_TRUE( captured ) << "nothing in a 40-second span was ever judged stale — the rule never fires";

    // And the boundary itself: exactly at the margin is still usable, one second past it is not. Stated
    // relative to the constant, so retuning the margin moves both sides together.
    const long long margin = TF::kSourceNewerMargin.count();
    EXPECT_EQ( TF::Judge( Seen( true, true, margin ) ), TF::Verdict::Show );
    EXPECT_EQ( TF::Judge( Seen( true, true, margin + 1 ) ), TF::Verdict::Capture );
}

// ---------------------------------------------------------------------------------------------------
// Unreadable stamps. The direction matters: absence of evidence must not become evidence of staleness,
// or an unreadable clock turns into an endless capture loop against a PNG that is probably fine.
// ---------------------------------------------------------------------------------------------------

TEST( ThumbnailFreshness, UnreadableStampsKeepTheExistingPicture )
{
    for ( const long long offset : { -100000LL, 0LL, 100000LL } )
    {
        EXPECT_EQ( TF::Judge( Seen( /*pngExists=*/true, /*readable=*/false, offset ) ), TF::Verdict::Show )
             << "an unread modtime was treated as proof of staleness (offset " << offset << ")";
    }
}

// The project links `gtest`, not `gtest_main`, so every suite here spells its own entry point out — the
// same four lines as ThumbnailKey and ThumbnailFraming next door.
int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
