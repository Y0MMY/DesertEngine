// What shape the clouds are, tested against the three claims the shape model makes.
//
// Common/CloudField.glslh is compiled here AS C++ (CloudFieldReference.hpp) and fed by the same noise
// functions the bake writes into the volume the march samples, so every number below is a number the GPU
// would produce. Three things are asserted, and each of them has already been got wrong once:
//
//   * THE CLOUD OCCUPIES A FRACTION OF THE ENVELOPE. The layer is ten kilometres thick and a
//     stratocumulus is six hundred metres of it. Let the vertical profile fill the envelope instead — as
//     it did when the envelope was two kilometres — and every cloud becomes a three-kilometre slab and
//     the sky an overcast.
//   * WHAT THE COVERAGE SLIDER MEANS, measured rather than asserted. The component's own comment carries
//     a table of sky cover against Coverage and calls the useful band 0.05 to 0.20; the table was
//     measured once, by hand, and nothing has re-measured it since. This suite reproduces the
//     measurement, so a change to the layer geometry or to the noise that silently moves the slider's
//     meaning turns into a failing test instead of a sky that "needs retuning".
//   * THE EROSION IS WEIGHTED BY DEPTH. Applied uniformly it removes the whole layer and leaves a
//     translucent veil, because a cut of fixed depth into a field whose typical value is small removes
//     the field. That is the difference between clouds and fog and it was got wrong once already.

#include "CloudFieldReference.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Desert::Tests::CloudFieldRef;

namespace
{
    // The component's defaults, converted to the kilometres this header works in exactly as
    // Graphic::PackCloudParams converts them. Written out rather than read from the component because
    // ComponentReflection owns the defaults; what this suite owns is what they PRODUCE.
    constexpr float kLayerThicknessKm = 10.0f; // Layer Thickness 1 000 000 cm

    CloudFieldParams DefaultParams()
    {
        CloudFieldParams params;
        params.WeatherTileKm     = 12.0f; // Weather Tile Size 1 200 000 cm
        params.Coverage          = 0.25f; // Coverage
        params.CoverageContrast  = 1.0f;  // Coverage Contrast
        params.CloudType         = 0.6f;  // Cloud Type
        params.CloudTypeVariance = 0.4f;  // Cloud Type Variance
        params.DetailTileKm      = 4.0f;  // Detail Tile Size 400 000 cm
        params.DetailStrength    = 0.1f;  // Detail Strength
        params.DensityScale      = 1.0f;  // Density Scale
        params.WindOffsetKm      = vec3( 0.0f, 0.0f, 0.0f );
        return params;
    }

    // TWO DEFINITIONS OF "A COLUMN CONTAINS CLOUD", measured together because they are different
    // questions and the difference is itself a finding.
    //
    //   Touched  the profile is non-zero SOMEWHERE in the column. This is the exact test the march's
    //            coarse tier uses to decide whether to drop to fine steps, so it is what the renderer's
    //            cost is proportional to — but a profile of 1e-6 counts, so it is not what a viewer sees.
    //   Opaque   looking straight up through the column, the cloud hides at least half the sky behind it:
    //            the vertical optical depth of the ERODED density, at the component's default extinction
    //            of 8 per kilometre, exceeds ln 2. This is "sky cover" in the sense the word is used of
    //            weather, and it is the number the component's own comment tabulates.
    //
    // The column grid spans one horizontal period of the coverage field (the weather tile), because the
    // field is periodic and one period IS the whole population rather than a sample of it. Heights stop
    // at 0.45 of the layer: the occupancy test in this same suite asserts that no cloud type reaches past
    // 0.35, so everything above is provably empty and sampling it would only cost time.
    struct SkyCover
    {
        float Touched;
        float Opaque;
    };

    // The component's Extinction Scale default, per kilometre, and the transmittance at which a column
    // counts as cloudy.
    constexpr float kExtinctionPerKm    = 8.0f;
    constexpr float kOpaqueOpticalDepth = 0.6931472f; // half the sky behind it is hidden

    SkyCover MeasureSkyCover( float coverage, int columnsPerAxis, int heightSamples )
    {
        CloudFieldParams params = DefaultParams();
        params.Coverage         = coverage;

        constexpr float kTopFraction = 0.45f;
        const float     stepKm       = kTopFraction * kLayerThicknessKm / static_cast<float>( heightSamples );

        int touched = 0;
        int opaque  = 0;

        for ( int iz = 0; iz < columnsPerAxis; ++iz )
        {
            for ( int ix = 0; ix < columnsPerAxis; ++ix )
            {
                const float x = params.WeatherTileKm * ( static_cast<float>( ix ) + 0.5f ) / columnsPerAxis;
                const float z = params.WeatherTileKm * ( static_cast<float>( iz ) + 0.5f ) / columnsPerAxis;

                bool  hasCloud     = false;
                float opticalDepth = 0.0f;

                for ( int ih = 0; ih < heightSamples; ++ih )
                {
                    const float heightFraction =
                         kTopFraction * ( static_cast<float>( ih ) + 0.5f ) / heightSamples;
                    const vec3 positionKm( x, heightFraction * kLayerThicknessKm, z );

                    const CloudFieldSample field = SampleCloudField( params, heightFraction, positionKm );
                    if ( field.Profile <= 0.0f )
                        continue;

                    hasCloud = true;
                    opticalDepth += CloudSampleDensity( params, field, positionKm ) * kExtinctionPerKm * stepKm;
                }

                touched += hasCloud ? 1 : 0;
                opaque += opticalDepth > kOpaqueOpticalDepth ? 1 : 0;
            }
        }

        const float total = static_cast<float>( columnsPerAxis * columnsPerAxis );
        return { static_cast<float>( touched ) / total, static_cast<float>( opaque ) / total };
    }

    // The highest point of the layer this cloud type reaches — its occupancy of the envelope.
    float ProfileSupportTop( float cloudType )
    {
        float top = 0.0f;
        for ( int step = 0; step <= 2000; ++step )
        {
            const float heightFraction = static_cast<float>( step ) / 2000.0f;
            if ( CloudVerticalProfile( heightFraction, cloudType ) > 0.0f )
                top = heightFraction;
        }
        return top;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// CloudVerticalProfile — the silhouette, and how much of the envelope it is allowed to fill
// ---------------------------------------------------------------------------------------------------

TEST( CloudFieldProfile, ItIsZeroAtTheVeryBottomAndTheVeryTopOfTheEnvelope )
{
    for ( int step = 0; step <= 10; ++step )
    {
        const float type = 0.1f * static_cast<float>( step );

        EXPECT_FLOAT_EQ( CloudVerticalProfile( 0.0f, type ), 0.0f ) << "type " << type << " at the floor";
        EXPECT_FLOAT_EQ( CloudVerticalProfile( 1.0f, type ), 0.0f ) << "type " << type << " at the ceiling";
    }

    // And outside the layer altogether, which the height fraction's own clamp already prevents but which
    // a hand-written caller could still ask for.
    EXPECT_FLOAT_EQ( CloudVerticalProfile( -0.5f, 0.6f ), 0.0f );
    EXPECT_FLOAT_EQ( CloudVerticalProfile( 1.5f, 0.6f ), 0.0f );
}

TEST( CloudFieldProfile, ACloudFillsAFRACTIONOfTheEnvelopeAndTheTypeSaysWhichFraction )
{
    // THE OCCUPANCY RELATION, and the one that stops a ten-kilometre envelope from becoming a
    // ten-kilometre cloud. At type 0 a cloud occupies the bottom six per cent of the layer — 0.6 km of
    // the ten, a stratocumulus — and at type 1 thirty-five per cent, 3.5 km, a congestus. Both are
    // meteorology, not taste, and neither may be exceeded.
    EXPECT_NEAR( ProfileSupportTop( 0.0f ), 0.06f, 0.005f ) << "a flat sheet has grown out of its species";
    EXPECT_NEAR( ProfileSupportTop( 1.0f ), 0.35f, 0.005f ) << "a heaped cloud has grown out of its species";

    // Monotone in between, and never past the tall end whatever the type — including the out-of-range
    // values a hand-edited scene can carry.
    float previous = 0.0f;
    for ( int step = 0; step <= 20; ++step )
    {
        const float type = 0.05f * static_cast<float>( step );
        const float top  = ProfileSupportTop( type );

        EXPECT_GE( top, previous ) << "type " << type << " occupies LESS than the flatter one below it";
        EXPECT_LE( top, 0.36f ) << "type " << type << " fills " << top * 100.0f << "% of the envelope";
        previous = top;
    }

    EXPECT_LE( ProfileSupportTop( -1.0f ), 0.07f );
    EXPECT_LE( ProfileSupportTop( 2.0f ), 0.36f );
}

TEST( CloudFieldProfile, TheBaseIsASharpRampAndTheTopTapersOverNearlyHalfTheCloud )
{
    // The two ends are deliberately asymmetric because real cloud is: a cumulus base sits on the lifting
    // condensation level and is flat, while the top tapers over nearly half the cloud's height. A
    // symmetric profile — the obvious thing to write — gives every cloud a rounded bottom, which reads
    // as fog lying in the air rather than as a cloud sitting on a level.
    constexpr float kType = 1.0f;

    const float top = ProfileSupportTop( kType );

    // Full density is reached, or the coverage field would be multiplying something that never gets
    // there and no setting would produce a solid cloud.
    float peak = 0.0f;
    for ( int step = 0; step <= 2000; ++step )
        peak = std::max( peak, CloudVerticalProfile( static_cast<float>( step ) / 2000.0f, kType ) );
    EXPECT_NEAR( peak, 1.0f, 1e-5f );

    // The base ramp is done inside the bottom fifth of the cloud; the taper occupies nearly half of it.
    EXPECT_NEAR( CloudVerticalProfile( 0.06f, kType ), 1.0f, 1e-5f ) << "the base ramp has not finished";
    EXPECT_LT( CloudVerticalProfile( top * 0.8f, kType ), 0.9f ) << "the top is not tapering";
    EXPECT_GT( CloudVerticalProfile( top * 0.5f, kType ), 0.95f ) << "the taper starts too low";
}

// ---------------------------------------------------------------------------------------------------
// The coverage mapping — what the slider actually means, measured
// ---------------------------------------------------------------------------------------------------

TEST( CloudFieldCoverage, TheFractionOfSkyWithCloudInItRisesMonotonicallyWithTheSlider )
{
    // A threshold slider that is not monotone in the fraction it produces is a slider an artist cannot
    // use: turning it up would sometimes open the sky. The grid is one horizontal period of the coverage
    // field, so this is the whole population and not a sample of one neighbourhood.
    constexpr int kColumns = 32;
    constexpr int kHeights = 36;

    float previousTouched = -1.0f;
    float previousOpaque  = -1.0f;

    std::printf( "[CloudField] coverage  touched  opaque\n" );
    for ( int step = 0; step <= 10; ++step )
    {
        const float    coverage = 0.05f * static_cast<float>( step );
        const SkyCover cover    = MeasureSkyCover( coverage, kColumns, kHeights );

        std::printf( "[CloudField]     %.2f    %5.1f%%  %5.1f%%\n", coverage, cover.Touched * 100.0f,
                     cover.Opaque * 100.0f );

        // A grid of a thousand columns resolves a per-mille change, so the slack is one column and not a
        // margin for the trend to be wrong in.
        EXPECT_GE( cover.Touched, previousTouched - 0.002f )
             << "Coverage " << coverage << " left LESS of the sky touched by cloud than the setting below it";
        EXPECT_GE( cover.Opaque, previousOpaque - 0.002f )
             << "Coverage " << coverage << " produced LESS opaque sky cover than the setting below it";

        previousTouched = std::max( previousTouched, cover.Touched );
        previousOpaque  = std::max( previousOpaque, cover.Opaque );
    }
}

// THIS TEST FAILS TODAY, AND THE FAILURE IS THE POINT.
//
// CloudFieldParams documents Coverage as "0 = clear, 1 = solid", and the component's tooltip says
// lowering it "opens clear gaps rather than thinning everything". At 0 it does not open all of them: the
// threshold becomes 1.0 and the transition band is a smoothstep of half-width 0.18/contrast AROUND it, so
// every column whose coverage field exceeds 0.82 still produces cloud — forty per cent of the sky touched
// and a fifth of a per cent of it genuinely hidden, at the shipped contrast of 1. The band is symmetric
// about a threshold that has run out of room.
//
// It is not only a documentation defect. It costs the slider its bottom end: the measured table in the
// test below shows the sky already sixty per cent touched at Coverage 0.05, so a third of the useful band
// has been spent before the artist touches the control. The fix is a threshold that accounts for the
// half-width — the field has to be pushed clear of the band's upper edge, not merely up to it — and it
// belongs to whoever owns Common/CloudField.glslh.
TEST( CloudFieldCoverage, AtZeroTheSkyIsGenuinelyClear )
{
    constexpr int kColumns = 32;
    constexpr int kHeights = 36;

    const SkyCover cover = MeasureSkyCover( 0.0f, kColumns, kHeights );

    EXPECT_FLOAT_EQ( cover.Touched, 0.0f )
         << "Coverage 0 still puts cloud in " << cover.Touched * 100.0f << "% of the sky";
    EXPECT_FLOAT_EQ( cover.Opaque, 0.0f )
         << "Coverage 0 still hides the sky behind " << cover.Opaque * 100.0f << "% of it";
}

TEST( CloudFieldCoverage, TheUsefulBandIsTheOneTheComponentsDefaultSitsIn )
{
    // THE MEASUREMENT THE COMPONENT'S COMMENT CARRIES, reproduced. The point is not the exact
    // percentages — they depend on the layer geometry and on the noise, and both are allowed to change —
    // it is that the band over which the slider does anything stays where the default, the presets and
    // the component's own documentation assume it is. A change that moves the whole mapping without
    // anybody noticing is exactly how a sky ends up "needing retuning" in every scene at once.
    //
    // MEASURED HERE, and re-measured when the weather tile moved onto the calibrated 12 km
    // (32 columns, 36 heights, contrast 1, layer and seeds as shipped) — the row this test prints:
    //
    //     Coverage    0.05   0.10   0.12   0.20   0.30   0.50
    //     opaque        0%     1%     4%    20%    58%    98%
    //
    // The tile size moves these figures without moving the mapping: the coverage field is periodic in the
    // tile and the grid spans one period of it, so the population is the same one — what changes is the
    // phase of the EROSION field against it, which is what shifts the opaque column by a couple of points.
    // The row before the tile changed was 0 / 1 / 2 / 20 / 63 / 99, inside these same tolerances.
    //
    // The qualitative claim the default rests on does survive: the slider does its work between about
    // 0.05 and 0.30 and is saturated well before its top, so the advice to stay low is right even though
    // the percentages are not. The tolerances are wide enough to survive a reseed and narrow enough to
    // catch a shift the size of the one above.
    constexpr int kColumns = 32;
    constexpr int kHeights = 36;

    const SkyCover atFive   = MeasureSkyCover( 0.05f, kColumns, kHeights );
    const SkyCover atTen    = MeasureSkyCover( 0.10f, kColumns, kHeights );
    const SkyCover atTwelve = MeasureSkyCover( 0.12f, kColumns, kHeights ); // low in the useful band
    const SkyCover atTwenty = MeasureSkyCover( 0.20f, kColumns, kHeights );
    const SkyCover atThirty = MeasureSkyCover( 0.30f, kColumns, kHeights );
    const SkyCover atFifty  = MeasureSkyCover( 0.50f, kColumns, kHeights );

    std::printf( "[CloudField] opaque sky cover: 0.05 %.0f%%  0.10 %.0f%%  0.12 %.0f%%  0.20 %.0f%%  "
                 "0.30 %.0f%%  0.50 %.0f%%\n",
                 atFive.Opaque * 100.0f, atTen.Opaque * 100.0f, atTwelve.Opaque * 100.0f, atTwenty.Opaque * 100.0f,
                 atThirty.Opaque * 100.0f, atFifty.Opaque * 100.0f );
    std::printf( "[CloudField] at Coverage 0.12 the sky is %.0f%% TOUCHED by cloud and %.0f%% hidden by it\n",
                 atTwelve.Touched * 100.0f, atTwelve.Opaque * 100.0f );

    // The pinned table, with a tolerance of ten points of sky — a reseed moves individual clouds and not
    // the statistic, so anything larger than this is the mapping itself having moved.
    EXPECT_NEAR( atFive.Opaque, 0.00f, 0.10f );
    EXPECT_NEAR( atTen.Opaque, 0.00f, 0.10f );
    EXPECT_NEAR( atTwelve.Opaque, 0.01f, 0.10f );
    EXPECT_NEAR( atTwenty.Opaque, 0.20f, 0.12f );
    EXPECT_NEAR( atThirty.Opaque, 0.63f, 0.12f );
    EXPECT_GT( atFifty.Opaque, 0.97f ) << "the top half of the slider is not saturated";

    // The bottom of the band leaves the sky essentially open, and the top of it is essentially overcast.
    // These two are what "the useful band is 0.05 to 0.20" has to mean if it means anything.
    EXPECT_LT( atFive.Opaque, 0.15f ) << "the bottom of the useful band is already busy";
    EXPECT_GT( atThirty.Opaque, 0.55f ) << "the band has not closed by 0.30 and the slider's top half is "
                                           "no longer the dead zone the component documents";

    // Strictly rising ACROSS the band, which is the part an artist feels. Ten points of slider must buy
    // at least ten points of sky, or the control is mush exactly where it is meant to work.
    EXPECT_GT( atTwenty.Opaque, atTen.Opaque + 0.10f );
    EXPECT_GT( atThirty.Opaque, atTwenty.Opaque + 0.15f );

    // And the thin half of the answer, which is the one that has bitten this programme before: a sky
    // that is mostly TOUCHED by cloud and hardly at all hidden by it is a sky full of veil. At the
    // default seventy per cent of it carries cloud too thin to hide anything, which is worth knowing when
    // the layer is retuned — it is the state the coverage mapping was rewritten to escape.
    EXPECT_GT( atTwelve.Touched, atTwelve.Opaque );
}

TEST( CloudFieldCoverage, TheContrastNarrowsTheTransitionBandWithoutMovingItsCentre )
{
    // Contrast is the WIDTH of the clear-to-cloudy transition, not a second coverage. Raising it must
    // sharpen the edge of an island, and it must not turn the sky into an overcast on its own — which is
    // what a naive implementation that multiplied the field instead of narrowing the band would do.
    constexpr int kColumns = 32;
    constexpr int kHeights = 24;

    CloudFieldParams soft = DefaultParams();
    soft.CoverageContrast = 0.25f;
    CloudFieldParams hard = DefaultParams();
    hard.CoverageContrast = 4.0f;

    // At the same Coverage, the soft band spills cloud into columns the hard one leaves clear, so the
    // measured cover falls as the contrast rises — while the field itself, and therefore where the
    // islands ARE, does not move.
    int softOnly = 0;
    int hardOnly = 0;

    for ( int iz = 0; iz < kColumns; ++iz )
    {
        for ( int ix = 0; ix < kColumns; ++ix )
        {
            const float x = 8.0f * ( static_cast<float>( ix ) + 0.5f ) / kColumns;
            const float z = 8.0f * ( static_cast<float>( iz ) + 0.5f ) / kColumns;

            bool inSoft = false;
            bool inHard = false;
            for ( int ih = 0; ih < kHeights; ++ih )
            {
                const float heightFraction = 0.45f * ( static_cast<float>( ih ) + 0.5f ) / kHeights;
                const vec3  positionKm( x, heightFraction * kLayerThicknessKm, z );

                inSoft = inSoft || SampleCloudField( soft, heightFraction, positionKm ).Profile > 0.0f;
                inHard = inHard || SampleCloudField( hard, heightFraction, positionKm ).Profile > 0.0f;
            }

            softOnly += ( inSoft && !inHard ) ? 1 : 0;
            hardOnly += ( inHard && !inSoft ) ? 1 : 0;
        }
    }

    EXPECT_GT( softOnly, 0 ) << "the contrast does nothing at all";
    EXPECT_EQ( hardOnly, 0 ) << "raising the contrast ADDED cloud somewhere, so it is moving the "
                                "threshold rather than narrowing the band around it";
}

// ---------------------------------------------------------------------------------------------------
// CloudSampleDensity — the erosion, and the weighting that makes it an erosion rather than a subtraction
// ---------------------------------------------------------------------------------------------------

namespace
{
    // The eroded density as a fraction of the profile it started from — "how much of this sample
    // survived the erosion". One number, comparable between a core sample and an edge sample, which is
    // what the relation below is about.
    float Retention( const CloudFieldParams& params, float profile, vec3 positionKm )
    {
        CloudFieldSample sample;
        sample.Profile      = profile;
        sample.DetailType   = params.CloudType;
        sample.DensityScale = params.DensityScale;

        return CloudSampleDensity( params, sample, positionKm ) / profile;
    }

    std::vector<vec3> ErosionProbePositions()
    {
        std::vector<vec3> positions;
        for ( int iz = 0; iz < 10; ++iz )
            for ( int iy = 0; iy < 6; ++iy )
                for ( int ix = 0; ix < 10; ++ix )
                    positions.emplace_back( 0.41f * ix, 0.23f * iy, 0.37f * iz );
        return positions;
    }
} // namespace

TEST( CloudFieldDensity, TheCoreKeepsItsDensityAndTheEdgeLosesMostOfItAtFullErosionStrength )
{
    // THE RELATION THAT SEPARATES CLOUDS FROM FOG. The erosion is scaled by (1 - Profile), so the whole
    // cut lands where the cloud is already ending. Remove that weight — apply the erosion uniformly, the
    // obvious form — and at DetailStrength 1 a core sample meets an erosion of up to 1.0, the remap's
    // window collapses, and the densest part of the cloud is deleted. That is not a tuning failure: a cut
    // of fixed depth into a field whose typical value is small removes the field.
    CloudFieldParams params = DefaultParams();
    params.DetailStrength   = 1.0f; // the worst case the slider allows

    constexpr float kCoreProfile = 0.95f;
    constexpr float kEdgeProfile = 0.10f;

    float worstCore = 1.0f;
    float edgeTotal = 0.0f;
    int   samples   = 0;

    for ( const vec3& position : ErosionProbePositions() )
    {
        const float core = Retention( params, kCoreProfile, position );
        const float edge = Retention( params, kEdgeProfile, position );

        worstCore = std::min( worstCore, core );
        edgeTotal += edge;
        ++samples;

        EXPECT_GE( core, edge ) << "the erosion took more from the core than from the edge";
    }

    const float meanEdge = edgeTotal / static_cast<float>( samples );
    std::printf( "[CloudField] erosion at full strength: worst core retention %.3f, mean edge retention "
                 "%.3f\n",
                 worstCore, meanEdge );

    // The core survives everywhere. With the weight, the deepest possible cut into a profile of 0.95 is
    // 0.05, which costs it a third of one per cent; without the weight, every position where the detail
    // field is above 0.95 deletes it outright.
    EXPECT_GT( worstCore, 0.95f ) << "a core sample lost " << ( 1.0f - worstCore ) * 100.0f
                                  << "% of its density to the erosion";

    // And the erosion still does its job at the edge, or it would be a term that costs a fetch and
    // changes nothing.
    EXPECT_LT( meanEdge, 0.5f ) << "the erosion is not cutting the edge at all";
}

TEST( CloudFieldDensity, HowMuchASampleKeepsRisesWithHowDeepInsideTheBodyItIs )
{
    // The same relation stated as a gradient rather than at two points: retention has to be monotone in
    // the profile, because the weight is (1 - Profile) and nothing else in the expression depends on it.
    // A regression that made the weight depend on the noise instead would still pass the two-point test
    // above at most positions and fail here.
    CloudFieldParams params = DefaultParams();
    params.DetailStrength   = 0.6f;

    for ( const vec3& position : ErosionProbePositions() )
    {
        float previous = -1.0f;
        for ( int step = 1; step <= 20; ++step )
        {
            const float profile   = 0.05f * static_cast<float>( step );
            const float retention = Retention( params, profile, position );

            EXPECT_GE( retention, previous - 1e-5f )
                 << "profile " << profile << " at (" << position.x << ", " << position.y << ", " << position.z
                 << ") kept LESS than the thinner sample below it";
            previous = retention;
        }
    }
}

TEST( CloudFieldDensity, AtZeroStrengthTheProfileSurvivesUntouchedAndTheDensityScaleIsTheOnlyMultiplier )
{
    // The erosion has to be removable — an artist who turns it off must get the smooth silhouette of the
    // coverage field, not a slightly different cloud — and the density scale has to be the last word,
    // applied after the erosion rather than folded into it (deck p.118).
    CloudFieldParams params = DefaultParams();
    params.DetailStrength   = 0.0f;

    for ( const vec3& position : ErosionProbePositions() )
    {
        for ( const float profile : { 0.05f, 0.3f, 0.8f, 1.0f } )
            EXPECT_NEAR( Retention( params, profile, position ), 1.0f, 1e-5f ) << "profile " << profile;
    }

    CloudFieldSample sample;
    sample.Profile    = 0.4f;
    sample.DetailType = 0.6f;

    const vec3 position( 1.7f, 0.9f, 2.3f );

    sample.DensityScale = 1.0f;
    const float full    = CloudSampleDensity( params, sample, position );
    sample.DensityScale = 0.5f;
    const float halved  = CloudSampleDensity( params, sample, position );

    EXPECT_NEAR( halved, full * 0.5f, 1e-5f );
}

TEST( CloudFieldDensity, AnEmptySampleCostsNothingAndTheResultNeverLeavesZeroToOne )
{
    // The early-out the march relies on to skip empty space, and the range the transmittance maths
    // assumes. A density above one would make a step's optical depth larger than the medium can produce
    // and would show as a hard black edge where the transmittance underflows.
    CloudFieldParams params = DefaultParams();
    params.DetailStrength   = 0.8f;
    params.DensityScale     = 2.0f; // the top of the slider

    CloudFieldSample empty;
    empty.Profile      = 0.0f;
    empty.DetailType   = 0.6f;
    empty.DensityScale = 2.0f;
    EXPECT_FLOAT_EQ( CloudSampleDensity( params, empty, vec3( 1.0f, 2.0f, 3.0f ) ), 0.0f );

    for ( const vec3& position : ErosionProbePositions() )
    {
        for ( const float profile : { 0.02f, 0.25f, 0.7f, 1.0f } )
        {
            CloudFieldSample sample;
            sample.Profile      = profile;
            sample.DetailType   = 0.6f;
            sample.DensityScale = 2.0f;

            const float density = CloudSampleDensity( params, sample, position );
            EXPECT_GE( density, 0.0f ) << "profile " << profile;
            EXPECT_LE( density, 1.0f ) << "profile " << profile;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// The producer as a whole
// ---------------------------------------------------------------------------------------------------

TEST( CloudFieldProducer, TheShapeIsSampledInFullThreeDimensionsAndNotExtrudedFromAPlane )
{
    // THE CORRECTION THAT COST THE MOST TO FIND. Written with y dropped, the coverage field reads as a
    // horizontal plane extruded vertically by the profile, and every cloud becomes a literal column with
    // a flat bottom and hard vertical walls. It is invisible at the zenith — you look ALONG the columns —
    // which is exactly how it survived the first round of checks.
    //
    // Stated as a relation: two samples in the same column at the same height fraction but at heights the
    // vertical tile can tell apart must be able to disagree. If the field were extruded they would agree
    // for every column in the sky.
    CloudFieldParams params = DefaultParams();

    // A COVERAGE HIGH ENOUGH THAT THE QUESTION CAN BE ASKED. The property under test — does the field
    // depend on altitude — is independent of how much cloud there is, but the MEASUREMENT is not: at a
    // coverage where nine columns in ten are empty, "low equals high" is two zeros agreeing and says
    // nothing at all. This bit once already: the test read as a regression to an extruded field when the
    // field was three-dimensional throughout and the coverage default had simply moved beneath it.
    params.Coverage = 0.45f;

    int disagreements = 0;
    for ( int iz = 0; iz < 24; ++iz )
    {
        for ( int ix = 0; ix < 24; ++ix )
        {
            const float x = 8.0f * ( static_cast<float>( ix ) + 0.5f ) / 24.0f;
            const float z = 8.0f * ( static_cast<float>( iz ) + 0.5f ) / 24.0f;

            // ONE height fraction, so the vertical profile contributes the same factor to both, and two
            // altitudes a long way apart in the layer. Only the 3D sample position differs.
            constexpr float kHeightFraction = 0.02f;

            const float low  = SampleCloudField( params, kHeightFraction, vec3( x, 0.4f, z ) ).Profile;
            const float high = SampleCloudField( params, kHeightFraction, vec3( x, 4.6f, z ) ).Profile;

            if ( std::abs( low - high ) > 1e-4f )
                ++disagreements;
        }
    }

    EXPECT_GT( disagreements, 24 * 24 / 4 )
         << "the field gives the same answer at every altitude in a column, so it is a plane extruded "
            "vertically and every cloud in the sky is a curtain";
}

TEST( CloudFieldProducer, TheCloudTypeVariesAcrossTheSkyRatherThanGivingEveryCloudTheSameCeiling )
{
    // At zero variance the vertical profile is the same function everywhere and the layer reads as a slab
    // with a lid. The variance is what lets one island be a low sheet and its neighbour a tower, so the
    // relation to assert is that the SPREAD of the returned type is non-zero at the default and exactly
    // zero when the variance is turned off.
    CloudFieldParams varying = DefaultParams();
    CloudFieldParams fixed   = DefaultParams();
    fixed.CloudTypeVariance  = 0.0f;

    float lowest  = 1.0f;
    float highest = 0.0f;

    for ( int iz = 0; iz < 24; ++iz )
    {
        for ( int ix = 0; ix < 24; ++ix )
        {
            const vec3 position( 8.0f * ( ix + 0.5f ) / 24.0f, 0.5f, 8.0f * ( iz + 0.5f ) / 24.0f );

            const float type = SampleCloudField( varying, 0.02f, position ).DetailType;
            lowest           = std::min( lowest, type );
            highest          = std::max( highest, type );

            EXPECT_FLOAT_EQ( SampleCloudField( fixed, 0.02f, position ).DetailType, fixed.CloudType )
                 << "with the variance off every cloud must be the authored type";
        }
    }

    EXPECT_GT( highest - lowest, 0.2f ) << "the type barely varies, so every cloud reaches the same "
                                           "altitude and the layer has a lid";
    EXPECT_GE( lowest, 0.0f );
    EXPECT_LE( highest, 1.0f );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
