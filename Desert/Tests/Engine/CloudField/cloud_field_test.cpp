// What shape the clouds are, tested against the claims the shape model makes.
//
// Common/CloudField.glslh is compiled here AS C++ (CloudFieldReference.hpp), fed by the same noise
// functions the generator writes into the volume the march samples AND by a real bake of the same
// Assets::BakeCloudProceduralVolume the renderer uploads, read through the trilinear REPEAT sampler the
// device would use. Every number below is therefore a number the GPU would produce.
//
// WHAT MOVED OUT OF THIS SUITE IN PHASE Э5, and why that is not a loss of coverage. Half of what stood
// here measured a PROFILE TABLE and a coverage threshold on the Alligator noise — the curve's two ends,
// the anvil's second hump, the quantile map, the placement anisotropy. None of those exists: the profile
// is the normalised distance field of a joined pile of lumps now, so the properties they asserted are
// properties of the GENERATOR and are asserted in Desert/Tests/Engine/CloudProceduralField, on the thing
// that decides them. A test of a table that no longer exists tests nothing, which is the same reasoning
// T1 used when the meteorological anchor moved to Desert/Tests/Engine/CloudType.
//
// WHAT IS ASSERTED HERE is what only this suite can see — the SEAM, compiled as C++:
//
//   * WHAT THE GENERATOR WRITES IS WHAT THE SHADER READS. Two million voxels nobody can inspect on the
//     device, addressed through a mapping with a half-texel in it and a wrap that has to be exact. This
//     is the relation the double compilation is FOR, and it is the one the profile table's own version of
//     this test was for before it.
//   * WHAT THE COVERAGE SLIDER MEANS, measured through the whole seam rather than on the bake alone.
//   * THE FIELD IS THREE-DIMENSIONAL. A producer whose silhouette is an extrusion of a plane is the
//     defect this entire programme exists to remove, and it survived one round of checks once already
//     because it is invisible at the zenith.
//   * THE EROSION IS WEIGHTED BY DEPTH. Applied uniformly it removes the whole layer and leaves a
//     translucent veil, because a cut of fixed depth into a field whose typical value is small removes
//     the field. That is the difference between clouds and fog and it was got wrong once already.
//   * FOUR SPECIES SHARE ONE SHELL and the union takes the deeper answer.

#include "CloudFieldReference.hpp"

// The LAYER's two erosion settings are the component's, and this suite is where their justification is
// measured — so they are read from the component rather than transcribed. PROPERTY expands to nothing, so
// this costs the suite no reflection and no registry.
#include <Engine/ECS/VolumetricCloudComponent.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace Desert::Tests::CloudFieldRef;

using Desert::Graphic::CloudTypeBaseKm;
using Desert::Graphic::CloudTypeShape;
using Desert::Graphic::CloudTypeTopKm;

namespace
{
    // THE FIXTURES ARE THIS SUITE'S OWN, and only one of them comes from elsewhere. What is tested here is
    // the MATHS — where the generator puts material, how the pattern axis moves it, whether the anvil is a
    // second lobe — so the shapes below are chosen to exercise it rather than to describe the weather.
    // The library that DOES describe the weather is content on disk now, and
    // Desert/Tests/Engine/CloudType is what holds it to meteorology: that anchor moved with the numbers
    // when T1 turned them into files, because a test of a table that no longer exists tests nothing.
    //
    // The one exception is the default, which is imported rather than copied: the coverage measurements
    // below are calibration data for the shipped sky, so they have to be made on the shape a scene with an
    // empty slot actually renders.
    const CloudTypeShape& DefaultShape()
    {
        return Desert::Assets::CloudTypeDefaultShape();
    }

    // THE LAST TWO OF EACH ROW ARE T3'S: the placement scale and the stretch along the wind. Every
    // fixture here carries the identity pair, because what this suite tests is the SEAM and a fixture that
    // also moved its patches would be changing two things between one measurement and the next.

    // A SHEET: nearly the same height everywhere in a patch, thin, low.
    constexpr CloudTypeShape kSheet{ 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f,  0.0f,
                                     0.0f,  0.05f, 0.50f, 0.70f, 0.75f, 1.00f, 1.00f };
    // A HEAP: a fair-weather cumulus, half its height at the rim of a patch.
    constexpr CloudTypeShape kHeap{ 0.90f, 1.90f, 0.45f, 0.06f, 0.45f, 0.0f,  0.0f,
                                    0.0f,  0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f };
    // A STORM: the only fixture with a second lobe, and the one the anvil is drawn from.
    constexpr CloudTypeShape kStorm{ 0.90f, 9.00f, 0.12f, 0.04f, 0.40f, 9.5f,  1.8f,
                                     0.85f, 0.85f, 1.00f, 1.35f, 1.30f, 1.00f, 1.00f };

    // The ENVELOPE the march intersects for a shape, computed the way Graphic::PackCloudParams computes it
    // rather than restated. There is no authored layer thickness — that is the whole point of the phase —
    // so a constant here would be a second statement of the type's own altitudes.
    float EnvelopeThicknessKm( const CloudTypeShape& shape )
    {
        return CloudTypeTopKm( shape ) - CloudTypeBaseKm( shape );
    }

    // The component's defaults, converted to the kilometres this header works in exactly as
    // Graphic::PackCloudParams converts them. The two EROSION settings are read from the component and no
    // longer transcribed, because this suite is where their justification is measured — a copy would be a
    // second statement of a number whose only argument is the assertions at the bottom of this file.
    // ComponentReflection still owns the census of defaults; what this suite owns is what they PRODUCE.
    //
    // COVERAGE AND ITS CONTRAST ARE NOT FIELDS OF THIS STRUCT ANY MORE. They decide what is IN the
    // modelling volume, so they are arguments to the bake and reach the seam through the bytes rather
    // than through the parameter block — which is why they are passed to CloudBindSpecies here.
    CloudFieldParams ParamsAtCoverage( float coverage )
    {
        const CloudTypeShape& shape = DefaultShape();

        // THE COMPONENT'S OWN TWO, read rather than copied. Both are calibrated against things this suite
        // MEASURES — the erosion's wave against a body's chord, the cut's depth against the surface the
        // eye sees — so a copy here would be a second statement of a number whose whole justification
        // lives in the assertions below it.
        const Desert::ECS::VolumetricCloudData shipped;

        CloudFieldParams params;
        params.DetailTileKm   = shipped.DetailTileSize / 100000.0f; // centimetres to kilometres
        params.DetailStrength = shipped.DetailStrength;
        // THE LAYER'S OWN SCALES, WITHOUT THE SPECIES' FACTORS IN THEM, which is where T3 moved the
        // product: with four kinds of cloud in one shell the multiply cannot be done once, so it happens
        // at the sample and the factor rides in CloudFieldSample.
        params.DensityScale = 1.0f;
        params.WindOffsetKm = vec3( 0.0f, 0.0f, 0.0f );

        // ONE SPECIES IN THE FIRST SLOT, volume and array together, which is what a layer with a single
        // type in it is. Every test below that does not say otherwise is testing that layer.
        CloudBindSpecies( params, &shape, 1u, vec3( 1.0f, 0.0f, 0.0f ), coverage, 1.0f );
        return params;
    }

    CloudFieldParams DefaultParams()
    {
        return ParamsAtCoverage( 0.35f );
    }

    /// One horizontal period of the field, kilometres — the region the volume was baked over, which IS
    /// the period because the bake wraps its lumps. A grid spanning it is the whole population rather than
    /// a sample of it.
    float PeriodKm()
    {
        return ModellingVolume().Params.RegionSizeKm;
    }

    /// The region's minimum corner, so a test can walk the period the volume was actually baked for.
    vec2 OriginKm()
    {
        return ModellingVolume().OriginKm;
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
    // field is periodic and one period IS the whole population rather than a sample of it. Heights span
    // the WHOLE envelope, because the envelope is now the species' own [base, top] and there is no empty
    // ceiling left above the cloud to skip.
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
        CloudFieldParams params = ParamsAtCoverage( coverage );

        const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );
        const float stepKm     = envelopeKm / static_cast<float>( heightSamples );

        int touched = 0;
        int opaque  = 0;

        for ( int iz = 0; iz < columnsPerAxis; ++iz )
        {
            for ( int ix = 0; ix < columnsPerAxis; ++ix )
            {
                const float x = OriginKm().x + PeriodKm() * ( static_cast<float>( ix ) + 0.5f ) / columnsPerAxis;
                const float z = OriginKm().y + PeriodKm() * ( static_cast<float>( iz ) + 0.5f ) / columnsPerAxis;

                bool  hasCloud     = false;
                float opticalDepth = 0.0f;

                for ( int ih = 0; ih < heightSamples; ++ih )
                {
                    const float heightFraction = ( static_cast<float>( ih ) + 0.5f ) / heightSamples;
                    const vec3  positionKm( x, heightFraction * envelopeKm, z );

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

} // namespace

// ---------------------------------------------------------------------------------------------------
// The profile generator — where a type's material lies, and whether three shapes are three shapes
//
// WHERE THE METEOROLOGY WENT. T0 asserted its four species' altitude bands here, because the library was
// a table compiled into the header this suite includes. T1 turned that library into nine files an artist
// can open, so the anchor moved to Desert/Tests/Engine/CloudType, which reads those files off the disk.
// The anchor is stronger there and would be vacuous here: this suite would be asserting meteorology about
// fixtures it wrote itself.
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
// The relation the double compilation is FOR: what the generator writes IS what the shader reads
// ---------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------
// THE VOLUME'S ADDRESSING — the two relations a sabotage run found nobody was keeping
// ---------------------------------------------------------------------------------------------------
//
// BOTH OF THESE WERE HOLES, and they were found the way the contract says holes are found: by breaking
// the thing a test claims to measure and watching the suite stay green. Nine sabotages, nine reds, two
// greens — and the two greens are these.

TEST( CloudFieldVolume, TheShaderAndTheGeneratorAgreeAboutHowTallTheVolumeIs )
{
    // THE RELATION: the shader pulls its vertical fetch in by half a texel, and "half a texel" is a
    // number only the volume's height defines. Stated in two places — a macro here, a constant in
    // Engine/Assets/CloudProceduralVolume.hpp — and until this line nothing compared them.
    //
    // WHAT IT COSTS TO GET WRONG is exactly what makes it hard to notice: at 64 instead of 32 the clamp
    // pulls in by half of the wrong texel, so the top and bottom rows of every cloud are read a sixty-
    // fourth of a layer out of place. On a 3.6 km shell that is 28 m of cloud base, which is invisible in
    // a frame and wrong in the field — the shape of defect the double compilation exists for.
    //
    // SETTING THE MACRO TO 64 LEFT BOTH SUITES GREEN. This is the assertion that was missing, and the
    // header that claimed a different suite was making it has been corrected.
    EXPECT_FLOAT_EQ( CLOUD_PROCEDURAL_VOLUME_HEIGHT,
                     static_cast<float>( Desert::Assets::kCloudProceduralVolumeHeight ) )
         << "Common/CloudField.glslh and Engine/Assets/CloudProceduralVolume.hpp disagree about how many "
            "rows the volume has, so the march's half-texel clamp is half of the wrong texel";
}

TEST( CloudFieldVolume, TheLayersCeilingDoesNotWrapOntoItsFloor )
{
    // THE PROPERTY THE PROFILE TABLE'S OWN TEST USED TO KEEP, and it was deleted with the table instead of
    // being carried across. Every sampler in this engine is REPEAT, so a vertical texture coordinate of
    // exactly 1 wraps to the bottom row: without the clamp, the TOP of the layer blends with its own FLOOR
    // and grows the bases of its clouds on top of itself. That is a defect with a name in this programme —
    // "the sky was a ceiling" — and removing the clamp left the suite green.
    //
    // MEASURED ON A LAYER WIDER THAN THE SPECIES' BAND, because that is the only arrangement in which the
    // two ends of the volume differ: an ordinary layer IS the union of its types' bands, so both ends are
    // empty and a wrap would be invisible. Here the cloud sits in the bottom quarter and the top rows are
    // air.
    const CloudTypeShape shape = kHeap; // a fair-weather cumulus, 0.90 to 1.90 km

    CloudModellingVolumeSelectOverLayer( &shape, 1u, /*coverage=*/0.85f, /*bottomKm=*/0.80f,
                                         /*thicknessKm=*/8.00f );

    CloudFieldParams params;
    params.DetailTileKm   = 4.0f;
    params.DetailStrength = 0.0f;
    params.DensityScale   = 1.0f;
    params.SpeciesCount   = 1;
    params.WindOffsetKm   = vec3( 0.0f );

    for ( int slot = 0; slot < CLOUD_SPECIES_SLOTS; ++slot )
        params.SpeciesEdge[slot] = vec4( 0.0f );
    params.SpeciesEdge[0] =
         vec4( shape.DetailCharacter, shape.DetailFactor, shape.DensityFactor, shape.ExtinctionFactor );

    params.RegionOriginKm  = ModellingVolume().OriginKm;
    params.InvRegionSizeKm = 1.0f / ModellingVolume().Params.RegionSizeKm;

    // FIRST: the coordinate itself never reaches either face.
    const vec3 atTop    = CloudProceduralVolumeUvw( params, 1.0f, vec3( 0.0f ) );
    const vec3 atBottom = CloudProceduralVolumeUvw( params, 0.0f, vec3( 0.0f ) );

    const float halfTexel = 0.5f / CLOUD_PROCEDURAL_VOLUME_HEIGHT;

    EXPECT_FLOAT_EQ( atTop.y, 1.0f - halfTexel )
         << "a height fraction of 1 addresses the volume's very edge, where a REPEAT sampler wraps";
    EXPECT_FLOAT_EQ( atBottom.y, halfTexel )
         << "a height fraction of 0 addresses the volume's very edge, where a REPEAT sampler wraps";

    // SECOND, AND THIS IS THE ONE THAT MATTERS: the consequence. At the top of this layer there is no
    // cloud, and there must be none however the coordinate is addressed. Without the clamp the fetch
    // blends the empty top row with the cloudy bottom one and half of the sky's ceiling fills in.
    int cloudyBelow = 0;
    int cloudyAtTop = 0;

    constexpr int kColumns = 48;

    for ( int iz = 0; iz < kColumns; ++iz )
    {
        for ( int ix = 0; ix < kColumns; ++ix )
        {
            const vec3 at( OriginKm().x + PeriodKm() * ( ix + 0.5f ) / kColumns, 0.0f,
                           OriginKm().y + PeriodKm() * ( iz + 0.5f ) / kColumns );

            // Where the cumulus actually is: 0.90 to 1.90 km in a layer that starts at 0.80 and is 8 km
            // thick, so a height fraction of about 0.08 is inside it.
            if ( SampleCloudField( params, 0.08f, at ).Profile > 0.0f )
                ++cloudyBelow;

            if ( SampleCloudField( params, 1.0f, at ).Profile > 0.0f )
                ++cloudyAtTop;
        }
    }

    std::printf( "[CloudFieldVolume] %d of %d columns carry cloud low in the layer; %d carry any at the "
                 "very top\n",
                 cloudyBelow, kColumns * kColumns, cloudyAtTop );

    // NOT VACUOUS: if the low band were empty too, "the top is empty" would be a statement about a volume
    // with nothing in it.
    ASSERT_GT( cloudyBelow, kColumns * kColumns / 8 )
         << "the fixture put almost no cloud in the layer, so the ceiling being empty means nothing";

    EXPECT_EQ( cloudyAtTop, 0 )
         << cloudyAtTop
         << " columns have cloud at the very top of a layer whose cloud is all in the bottom eighth — the "
            "vertical read wrapped onto the floor and the sky grew its own cloud bases on its ceiling";
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
        const float    coverage = 0.10f * static_cast<float>( step );
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

// THE END THAT HAS TO BE EXACT, AND WAS NOT, TWICE.
//
// This test was written failing: the threshold's transition band was symmetric about a threshold that had
// run out of room, so at Coverage 0 every column whose coverage field exceeded 0.82 still produced cloud —
// forty per cent of the sky touched at a setting documented as clear. It was then fixed by pushing the
// threshold clear of the band rather than up to it, and it has been green since.
//
// IT IS EXACT BY CONSTRUCTION NOW and not by arithmetic that happens to cancel. A cell carries a cluster
// when its own hash falls below the alive fraction, and the alive fraction is `pow(Coverage, 0.68)`, which
// is zero at zero — there is no band to run out of room. That is a stronger guarantee than the one this
// test was written to check, and the test is kept because the guarantee is what matters and not the
// mechanism that provides it.
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
        sample.DetailType   = params.SpeciesEdge[0].x;
        // ONE, not the params' scale: what is measured here is the fraction of the profile that survives
        // the EROSION, and the density scale is a multiplier applied after it. Since the packer folds the
        // species' own density into that scale, leaving it in would make every retention read 1.15 for a
        // congestus and 0.70 for a stratus, which says nothing about erosion at all.
        sample.DensityScale = 1.0f;
        // The erosion depth is the WINNING species' factor times the layer's strength, so a fixture that
        // left this at zero would erode nothing whatever the strength said.
        sample.DetailFactor     = params.SpeciesEdge[0].y;
        sample.ExtinctionFactor = params.SpeciesEdge[0].w;

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

    // ALL FIVE FIELDS, and the two nobody reads here are the reason this comment exists. CloudFieldSample
    // is a GLSL struct compiled as C++, so it has no default member initialisers — GLSL has none to give
    // it — and a fixture that fills three of five hands CloudSampleDensity two indeterminate floats. That
    // is not a theoretical complaint: it turned macOS Release red, because at -O2 clang gives each by-value
    // argument its OWN stack slot, so the two calls below read two DIFFERENT leftovers. One came back
    // finite and the other with an all-ones exponent, and `DetailStrength * (+inf)` at a strength of zero
    // is NaN. At -O0 there is a single slot, both calls read the same bytes, and the suite is green — which
    // is why only Release could ever see it.
    CloudFieldSample sample;
    sample.Profile          = 0.4f;
    sample.DetailType       = 0.6f;
    sample.DetailFactor     = params.SpeciesEdge[0].y;
    sample.ExtinctionFactor = params.SpeciesEdge[0].w;

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

    // ALL FIVE FIELDS — see the note in the test above. A sample built with three of them fills the other
    // two with whatever the stack held, and the bounds asserted below are exactly the assertions a NaN
    // slips through in the direction that reads as a broken build rather than a broken cloud.
    CloudFieldSample empty;
    empty.Profile          = 0.0f;
    empty.DetailType       = 0.6f;
    empty.DensityScale     = 2.0f;
    empty.DetailFactor     = params.SpeciesEdge[0].y;
    empty.ExtinctionFactor = params.SpeciesEdge[0].w;
    EXPECT_FLOAT_EQ( CloudSampleDensity( params, empty, vec3( 1.0f, 2.0f, 3.0f ) ), 0.0f );

    for ( const vec3& position : ErosionProbePositions() )
    {
        for ( const float profile : { 0.02f, 0.25f, 0.7f, 1.0f } )
        {
            CloudFieldSample sample;
            sample.Profile          = profile;
            sample.DetailType       = 0.6f;
            sample.DensityScale     = 2.0f;
            sample.DetailFactor     = params.SpeciesEdge[0].y;
            sample.ExtinctionFactor = params.SpeciesEdge[0].w;

            const float density = CloudSampleDensity( params, sample, position );
            EXPECT_GE( density, 0.0f ) << "profile " << profile;
            EXPECT_LE( density, 1.0f ) << "profile " << profile;
        }
    }
}

TEST( CloudFieldDensity, ASliderAtZeroTimesAMaterialFactorOutOfRangeIsStillANumber )
{
    // THE RELATION THE TWO TESTS ABOVE ASSUME AND NEITHER STATES: every product of a LAYER slider and a
    // per-body FACTOR in this file has to stay finite, because the march cannot survive one that does not.
    // A NaN density is not a wrong cloud — it is a NaN optical depth, therefore a NaN transmittance, and it
    // shows as a black or fully transparent hole with nothing in the log.
    //
    // The two operands come from opposite sides of the seam and only one of them is validated. Detail
    // Strength and Density Scale are sliders whose ZERO is a documented, invited position — the test above
    // exists precisely to assert that turning the erosion off works. The factors are asset and component
    // data: the procedural side is held to [0, 8] by Assets::ValidateCloudTypeShape, the AUTHORED side to
    // nothing at all, because a reflected Range constrains the editor's slider and the deserialiser clamps
    // nothing. So the shader has to be the bound, and `max(x, 0)` is only half of one: it leaves +inf
    // intact, and `0 * (+inf)` is NaN.
    //
    // This is written as a sweep over BOTH operands rather than as a single probe because the defect it
    // guards needs the pair — the factor alone is harmless at any strength above zero, and the strength
    // alone is harmless at any finite factor.
    CloudFieldParams params = DefaultParams();

    const float hostile[] = {
         0.0f, 1.0f, 8.0f, 1e30f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max() };

    for ( const float sliderStrength : { 0.0f, 0.4f, 1.0f } )
    {
        for ( const float sliderDensity : { 0.0f, 1.0f, 2.0f } )
        {
            for ( const float factor : hostile )
            {
                params.DetailStrength = sliderStrength;
                params.DensityScale   = sliderDensity;

                CloudFieldSample sample;
                sample.Profile          = 0.4f;
                sample.DetailType       = 0.6f;
                sample.DensityScale     = sliderDensity;
                sample.DetailFactor     = factor;
                sample.ExtinctionFactor = factor;

                const float density = CloudSampleDensity( params, sample, vec3( 1.7f, 0.9f, 2.3f ) );

                EXPECT_TRUE( std::isfinite( density ) )
                     << "DetailStrength " << sliderStrength << " x DetailFactor " << factor << " gave " << density;
                EXPECT_GE( density, 0.0f );
                EXPECT_LE( density, 1.0f );
            }
        }
    }
}

TEST( CloudFieldDensity, TheProducerBoundsTheSpeciesRowRatherThanHandingItToTheMarchAsItFoundIt )
{
    // THE SAME ARITHMETIC ONE LEVEL UP, and it was found by looking for siblings of the defect above
    // rather than by a failure. `DensityScale` is composed in CloudSampleProceduralField as
    // `layer slider * the species' Density Factor`, which is the identical `slider * factor` shape — and
    // the layer slider's zero is just as legitimate a position as the erosion's, because "thin this whole
    // deck away to nothing" is what the bottom of the Density Scale slider means.
    //
    // It is asserted through SampleCloudField rather than on the sample, because the point is that the
    // PRODUCER bounds the row: a march that receives an infinite DensityScale has already lost, whatever
    // CloudSampleDensity does with it afterwards.
    //
    // The row is written directly instead of through a cloud type, and that is the whole reason the test
    // can exist: Assets::ValidateCloudTypeShape would refuse to load a shape carrying these numbers, so a
    // fixture built from an asset can never reach this path. The AUTHORED producer has no such validator
    // in front of it at all, which is what makes this shape worth bounding rather than trusting.
    //
    // THE SAMPLE HAS TO CONTAIN CLOUD, AND THAT IS NOT A DETAIL. The composition being tested lives inside
    // `if ( speciesProfile > result.Profile )` — the branch a species takes when it WINS the union. Ask
    // about a point of clear sky and the loop `continue`s, DensityScale stays at its zeroed 0, and the
    // assertion passes without having executed the line it is about. This test was written that way first
    // and stayed green against the unbounded version; the count below is what turned it into a test.
    //
    // THE VOLUME IS BAKED ONCE, OUTSIDE THE SWEEP. ParamsAtCoverage runs a full
    // Assets::BakeCloudProceduralVolume, and the shape is the same on every iteration — so calling it
    // inside the loop baked the identical two million voxels twelve times over. It cost 1.8 s in release
    // and over three minutes in debug, which is a test nobody will keep. What the sweep varies is the
    // SPECIES ROW and the layer's slider, and both are plain fields of the parameter block.
    const CloudFieldParams baked      = ParamsAtCoverage( 0.9f );
    const float            envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    int winners = 0;

    for ( const float hostile :
          { std::numeric_limits<float>::infinity(), 1e30f, std::numeric_limits<float>::max(), -1.0f } )
    {
        for ( const float slider : { 0.0f, 1.0f, 2.0f } )
        {
            CloudFieldParams params = baked;
            params.DensityScale     = slider;
            params.SpeciesEdge[0].y = hostile; // Detail Factor
            params.SpeciesEdge[0].z = hostile; // Density Factor
            params.SpeciesEdge[0].w = hostile; // Extinction Factor

            for ( int iz = 0; iz < 6; ++iz )
                for ( int ix = 0; ix < 6; ++ix )
                {
                    const float fraction = 0.5f;
                    const vec3  at( OriginKm().x + PeriodKm() * ( ix + 0.5f ) / 6.0f, fraction * envelopeKm,
                                    OriginKm().y + PeriodKm() * ( iz + 0.5f ) / 6.0f );

                    const CloudFieldSample field = SampleCloudField( params, fraction, at );
                    if ( !( field.Profile > 0.0f ) )
                        continue;

                    ++winners;

                    EXPECT_TRUE( std::isfinite( field.DensityScale ) )
                         << "Density Scale " << slider << " x Density Factor " << hostile
                         << " left the producer as " << field.DensityScale;
                    EXPECT_TRUE( std::isfinite( field.DetailFactor ) ) << "Detail Factor " << hostile;
                    EXPECT_TRUE( std::isfinite( field.ExtinctionFactor ) ) << "Extinction Factor " << hostile;

                    EXPECT_GE( field.DensityScale, 0.0f );
                    EXPECT_GE( field.DetailFactor, 0.0f );
                    EXPECT_GE( field.ExtinctionFactor, 0.0f );

                    const float density = CloudSampleDensity( params, field, at );
                    EXPECT_TRUE( std::isfinite( density ) ) << "factor " << hostile << " slider " << slider;
                    EXPECT_GE( density, 0.0f );
                    EXPECT_LE( density, 1.0f );
                }
        }
    }

    EXPECT_GT( winners, 100 ) << "no species ever won the union, so the composition under test never ran";
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
    // A COVERAGE HIGH ENOUGH THAT THE QUESTION CAN BE ASKED. The property under test — does the field
    // depend on altitude — is independent of how much cloud there is, but the MEASUREMENT is not: at a
    // coverage where nine columns in ten are empty, "low equals high" is two zeros agreeing and says
    // nothing at all. This bit once already: the test read as a regression to an extruded field when the
    // field was three-dimensional throughout and the coverage default had simply moved beneath it.
    CloudFieldParams params = ParamsAtCoverage( 0.6f );

    const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    int disagreements = 0;
    for ( int iz = 0; iz < 24; ++iz )
    {
        for ( int ix = 0; ix < 24; ++ix )
        {
            const float x = OriginKm().x + PeriodKm() * ( static_cast<float>( ix ) + 0.5f ) / 24.0f;
            const float z = OriginKm().y + PeriodKm() * ( static_cast<float>( iz ) + 0.5f ) / 24.0f;

            // TWO HEIGHT FRACTIONS IN THE SAME COLUMN, low in the shell and high in it. The horizontal
            // position is identical, so anything that differs is the field's own dependence on altitude —
            // and a producer that extruded a plane would answer the same at both.
            const float low  = SampleCloudField( params, 0.15f, vec3( x, 0.15f * envelopeKm, z ) ).Profile;
            const float high = SampleCloudField( params, 0.75f, vec3( x, 0.75f * envelopeKm, z ) ).Profile;

            if ( std::abs( low - high ) > 1e-4f )
                ++disagreements;
        }
    }

    EXPECT_GT( disagreements, 24 * 24 / 4 )
         << "the field gives the same answer at every altitude in a column, so it is a plane extruded "
            "vertically and every cloud in the sky is a curtain";
}

// ---------------------------------------------------------------------------------------------------
// SEVERAL KINDS OF CLOUD IN ONE SKY — the whole of T3, and the relation that guards it
// ---------------------------------------------------------------------------------------------------

namespace
{
    // A LOW DECK AND A TALL TOWER, which is the accepting frame of this phase stated as two shapes. The
    // bands OVERLAP between 2.2 and 2.6 km, and that overlap is what every test below is about: it is
    // exactly the region a partition of one field could never have produced, because weights that sum to
    // one cannot both be non-zero.
    //
    // The deck is the shipped Stratocumulus' geometry with its own small placement scale; the tower is the
    // built-in congestus at the layer's scale. Written out rather than loaded from disk because this suite
    // does not read files, and because what is being tested is the arithmetic of the union rather than the
    // library's numbers — Desert/Tests/Engine/CloudType owns those.
    constexpr CloudTypeShape kDeck{ 0.60f, 2.60f, 0.80f, 0.10f, 0.35f, 0.0f,  0.0f,
                                    0.0f,  0.95f, 0.70f, 0.95f, 1.00f, 0.35f, 1.60f };
    constexpr CloudTypeShape kTower{ 2.20f, 5.80f, 0.15f, 0.04f, 0.50f, 0.0f,  0.0f,
                                     0.0f,  1.00f, 1.00f, 1.15f, 1.00f, 1.00f, 1.00f };

    CloudFieldParams TwoSpeciesParams( float coverage )
    {
        const CloudTypeShape pair[2] = { kDeck, kTower };

        CloudFieldParams params;
        params.DetailTileKm   = 4.0f;
        params.DetailStrength = 0.1f;
        params.DensityScale   = 1.0f;
        params.WindOffsetKm   = vec3( 0.0f, 0.0f, 0.0f );

        // Binding the species BAKES the volume for exactly this set, which is what makes the two channels
        // below the two channels the device would carry.
        CloudBindSpecies( params, pair, 2u, vec3( 1.0f, 0.0f, 0.0f ), coverage, 1.0f );
        return params;
    }

    // The height fraction of an absolute altitude inside the SET's envelope — the axis the table is built
    // on and the axis the march hands the field.
    float FractionOfSetEnvelope( float altitudeKm )
    {
        const CloudTypeShape                   pair[2]  = { kDeck, kTower };
        const Desert::Graphic::CloudEnvelopeKm envelope = Desert::Graphic::CloudTypeSetEnvelopeKm( pair, 2u );

        return ( altitudeKm - envelope.BottomKm ) / ( envelope.TopKm - envelope.BottomKm );
    }
} // namespace

TEST( CloudFieldSpecies, TheEnvelopeIsTheUnionAndContainsEveryMemberOfTheSet )
{
    // THE RELATION THIS SUBSYSTEM GETS WRONG SILENTLY. A shell that does not contain a type does not
    // error; it slices the top off that type's cloud, and the symptom is a sky with a ceiling nobody
    // remembers setting (commit 54330ab9). T0 asserted it for a set of one; the claim T1 made was that
    // widening the set would not need the code rewritten, so this is the same relation over a set.
    const CloudTypeShape all[4] = { kDeck, kTower, kSheet, kStorm };

    for ( std::uint32_t count = 1; count <= 4; ++count )
    {
        const Desert::Graphic::CloudEnvelopeKm envelope = Desert::Graphic::CloudTypeSetEnvelopeKm( all, count );

        for ( std::uint32_t slot = 0; slot < count; ++slot )
        {
            EXPECT_LE( envelope.BottomKm, CloudTypeBaseKm( all[slot] ) )
                 << "the shell starts above species " << slot << " of " << count;
            EXPECT_GE( envelope.TopKm, CloudTypeTopKm( all[slot] ) )
                 << "the shell ends below species " << slot << " of " << count;
        }

        // AND IT IS NOT LARGER THAN IT HAS TO BE. A generous envelope satisfies containment and wastes the
        // whole altitude axis of the table on air nothing can put cloud in, so both ends must be SOME
        // species' own.
        bool bottomIsSomebodys = false;
        bool topIsSomebodys    = false;
        for ( std::uint32_t slot = 0; slot < count; ++slot )
        {
            bottomIsSomebodys = bottomIsSomebodys || envelope.BottomKm == CloudTypeBaseKm( all[slot] );
            topIsSomebodys    = topIsSomebodys || envelope.TopKm == CloudTypeTopKm( all[slot] );
        }
        EXPECT_TRUE( bottomIsSomebodys ) << "the shell's floor belongs to no species in the set";
        EXPECT_TRUE( topIsSomebodys ) << "the shell's ceiling belongs to no species in the set";
    }
}

namespace
{
    // ONE SPECIES' PROFILE AT ONE POINT, read the way the producer reads it.
    //
    // IT IS A CHANNEL OF THE VOLUME, and that is the whole of what phase Э5 did to this mirror. It used
    // to rebuild the species' own placement frame, fetch the coverage noise in it, apply the quantile map
    // and read the profile table — four steps that could drift from the producer, which is why the test
    // below compares the union against the max of this rather than trusting either. Now there is one
    // fetch and one channel, and the drift it can suffer is the addressing: that is exactly the relation
    // the sampler mirror in CloudFieldReference.hpp exists to keep.
    float SpeciesProfileAt( const CloudFieldParams& params, int slot, float fraction, vec3 positionKm )
    {
        const vec4 volume = CLOUD_SAMPLE_MODELLING( CloudProceduralVolumeUvw( params, fraction, positionKm ) );
        return clamp( volume[slot], 0.0f, 1.0f );
    }
} // namespace

TEST( CloudFieldSpecies, TwoSpeciesCanOccupyTheSamePointAndTheUnionTakesTheDeeperOne )
{
    // THE FRAME THIS PHASE EXISTS FOR, reduced to the one number that makes it possible: there is a point
    // of the sky at which BOTH species have a non-zero profile at once.
    //
    // A partition — one placement field sliced between the species, weights summing to one — cannot
    // produce such a point at all, by arithmetic and not by tuning. That construction was written into the
    // plan and taken out again (D-14), and this test is what stops it coming back: it fails the moment the
    // weights are made to sum to anything.
    CloudFieldParams params = TwoSpeciesParams( 0.55f );

    // The bands overlap on [2.2, 2.6] km. Sampled in the middle of that, where both are alive.
    const float altitudeKm = 2.40f;
    const float fraction   = FractionOfSetEnvelope( altitudeKm );

    int bothPresent = 0;
    int unionWrong  = 0;
    int winnerWrong = 0;

    constexpr int kColumns = 96;

    for ( int iz = 0; iz < kColumns; ++iz )
    {
        for ( int ix = 0; ix < kColumns; ++ix )
        {
            // The producer is handed the ALTITUDE ABOVE THE SHELL'S FLOOR in y, exactly as the march hands
            // it, and the height fraction separately.
            const vec3 position( OriginKm().x + PeriodKm() * ( ix + 0.5f ) / kColumns,
                                 altitudeKm - kDeck.BaseAltitudeKm,
                                 OriginKm().y + PeriodKm() * ( iz + 0.5f ) / kColumns );

            const float deck  = SpeciesProfileAt( params, 0, fraction, position );
            const float tower = SpeciesProfileAt( params, 1, fraction, position );

            const CloudFieldSample united = SampleCloudField( params, fraction, position );

            if ( std::abs( united.Profile - std::max( deck, tower ) ) > 1e-5f )
                ++unionWrong;

            if ( deck <= 0.0f || tower <= 0.0f )
                continue;

            ++bothPresent;

            // AND THE WINNER TAKES ITS OWN CHARACTER, unaveraged. This is the other half of D-14: a
            // stratocumulus edge on the tower, or an average of the two, is the smear the winner-take-all
            // rule exists to prevent.
            // `>=` AND NOT `>`, and the difference is one column in 657. The volume is quantised to a
            // 255th, so two species' channels are exactly equal often enough to matter — and the producer
            // walks the slots in order taking a species only when it STRICTLY exceeds the best so far, so
            // a tie is won by the EARLIER slot. A mirror written with `>` disagrees with the shader on
            // exactly those ties, which is a defect in the mirror and not in the union.
            const float expected = deck >= tower ? kDeck.DetailCharacter : kTower.DetailCharacter;
            if ( std::abs( united.DetailType - expected ) > 1e-5f )
                ++winnerWrong;
        }
    }

    std::printf( "[CloudFieldSpecies] of %d columns, %d carried BOTH the deck and the tower at %.2f km\n",
                 kColumns * kColumns, bothPresent, altitudeKm );

    EXPECT_GT( bothPresent, 0 ) << "no point of the sky held two kinds of cloud at once — which is what a "
                                   "partition of one field would produce, and what D-14 rejected";
    EXPECT_EQ( unionWrong, 0 ) << "the union is not the max of the two profiles";
    EXPECT_EQ( winnerWrong, 0 ) << "the winning species did not keep its own edge character";
}

TEST( CloudFieldSpecies, AnEmptySetStillHasASkyAndAnUnfilledSlotCostsNothing )
{
    // THE EMPTY SET IS A DOCUMENTED ANSWER, and the renderer's answer to it is one built-in congestus —
    // which is a set of one and therefore already covered above. What is tested HERE is the other half of
    // that promise: that slots past the count cannot put cloud in the sky even if their arrays were
    // filled, because the profile table's channel for them is zero everywhere.
    CloudFieldParams params;
    params.DetailTileKm   = 4.0f;
    params.DetailStrength = 0.1f;
    params.DensityScale   = 1.0f;
    params.WindOffsetKm   = vec3( 0.0f, 0.0f, 0.0f );

    // Deliberately hostile: the VOLUME is baked for ONE species, so channel 1 is zero everywhere, and
    // then the array is filled with a SECOND species carrying a large density. Only the count and that
    // empty channel stand between it and the frame — which is the same two independent reasons the
    // profile table's empty channel used to give, on the resource that replaced it.
    const CloudTypeShape one[1] = { kTower };
    CloudBindSpecies( params, one, 1u, vec3( 1.0f, 0.0f, 0.0f ), 0.9f, 1.0f );

    params.SpeciesEdge[1] =
         vec4( kDeck.DetailCharacter, kDeck.DetailFactor, kDeck.DensityFactor, kDeck.ExtinctionFactor );

    const float envelopeKm = EnvelopeThicknessKm( kTower );

    for ( int i = 0; i < 64; ++i )
    {
        const vec3 position( 37.0f * i / 64.0f, 0.5f * envelopeKm, 11.0f * i / 64.0f );

        const CloudFieldSample sample = SampleCloudField( params, 0.5f, position );
        if ( sample.Profile <= 0.0f )
            continue;

        EXPECT_FLOAT_EQ( sample.DetailType, kTower.DetailCharacter )
             << "a slot past the species count reached the frame";
    }

    // And the volume itself: channels 1, 2 and 3 of a one-species bake are zero at every voxel, which is
    // the second, independent reason the slot cannot be read. Walked over a lattice of positions rather
    // than over the bytes, because what has to be zero is what the SAMPLER returns — a trilinear filter
    // between four zero texels is the only thing that makes "the channel is empty" true of the field
    // rather than of the storage.
    const float envelopeUnusedKm = EnvelopeThicknessKm( kTower );

    for ( int i = 0; i <= 32; ++i )
    {
        const float fraction = static_cast<float>( i ) / 32.0f;

        for ( int j = 0; j < 8; ++j )
        {
            const vec3 position( OriginKm().x + PeriodKm() * ( j + 0.5f ) / 8.0f, fraction * envelopeUnusedKm,
                                 OriginKm().y + PeriodKm() * ( j * 3 + 1.5f ) / 8.0f );

            const vec4 volume = CLOUD_SAMPLE_MODELLING( CloudProceduralVolumeUvw( params, fraction, position ) );

            EXPECT_FLOAT_EQ( volume.y, 0.0f ) << "an unwritten channel is not zero";
            EXPECT_FLOAT_EQ( volume.z, 0.0f ) << "an unwritten channel is not zero";
            EXPECT_FLOAT_EQ( volume.w, 0.0f ) << "an unwritten channel is not zero";
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// THE EROSION AGAINST WHAT IT CUTS INTO — task DS, 2026-08-24
// ---------------------------------------------------------------------------------------------------
//
// WHY THIS SECTION EXISTS. `Detail Strength` had stood at 0.10 since phase T2b and `Detail Tile Size` at
// four kilometres since before that, and every frame this subsystem has ever produced reads as smooth.
// The suspicion was that phase Э5 had moved the numbers' carrying input — that the profile used to be low
// almost everywhere so a shallow cut sufficed, and that the normalised distance field is high inside
// bodies so the same cut does nothing.
//
// MEASURED ON BOTH PRODUCERS, THAT IS BACKWARDS. The pre-Э5 field carried 60.8 % of its profile MASS
// above 0.9 against this one's 20.7 %, and at 0.10 the erosion removed 1.7 % of the old field's mass
// against 7.1 % of this one's. The cut got four times STRONGER when the producer changed. The old frame
// is as smooth as the new one (`Docs/Clouds/Shots/E5a_before_mid_away.png`), which is the same finding
// arrived at from the picture.
//
// WHAT WAS ACTUALLY WRONG is a relation of the family this programme has been bitten by four times —
// "what is placed against what can resolve it" — and it had been wrong on BOTH producers:
//
//     the erosion's own wavelength, 884 m at the four-kilometre tile
//     against a body's own chord,   1071 m
//
// One wave across a whole cloud. A field that is nearly constant over a body cannot cut billows into it;
// it makes that body slightly larger on one side and slightly smaller on the other, which is a smooth
// blob of a different size. The three assertions below are that relation and its two ends.

namespace
{
    // The erosion's own noise term, mirrored from CloudSampleDensity so that a sweep over the strength
    // costs one walk of the field rather than one per setting. It is VERIFIED against the source rather
    // than trusted — TheMirrorOfTheErosionAgreesWithTheShader is the first test below, and every other
    // test in this section rests on it.
    float ErosionNoiseAt( const CloudFieldParams& params, const CloudFieldSample& field, vec3 positionKm )
    {
        const vec3 windPos( positionKm.x - params.WindOffsetKm.x, positionKm.y - params.WindOffsetKm.y,
                            positionKm.z - params.WindOffsetKm.z );

        const float tile = std::max( params.DetailTileKm, 1e-4f );
        const vec3  detailPos( windPos.x * CLOUD_DETAIL_FREQ_X / tile, windPos.y * CLOUD_DETAIL_FREQ_Y / tile,
                               windPos.z * CLOUD_DETAIL_FREQ_Z / tile );

        // THE SAMPLE'S OWN SLOT, so the mirror reads whichever of the layer's volumes the winning species
        // named — the same argument the seam passes. A mirror hardwired to slot 0 would agree with a
        // shader that ignored the slot entirely, which is precisely the defect phase NV removed.
        const vec4 noise = CLOUD_SAMPLE_NOISE( field.NoiseSlot, detailPos );

        const float wispy   = glm::mix( noise.x, noise.y, field.Profile );
        const float billowy = glm::mix( noise.z, noise.w, std::pow( field.Profile, 0.25f ) );

        return glm::clamp( glm::mix( wispy, billowy, glm::clamp( field.DetailType, 0.0f, 1.0f ) ), 0.0f, 1.0f );
    }

    /// The eroded density from the pair, at an EFFECTIVE depth t = clamp(strength * the type's factor).
    float ErodedDensity( float profile, float erosionNoise, float t )
    {
        const float erosion = erosionNoise * t * ( 1.0f - profile );
        return glm::clamp( ( profile - erosion ) / std::max( 1.0f - erosion, 1e-6f ), 0.0f, 1.0f );
    }

    /// The finest chord the march is relied on to FIND, metres. Every bound in this section is stated
    /// against it rather than against a constant, so lowering Max Steps moves the bound with it.
    double MarchResolvableM()
    {
        return Desert::Graphic::CloudFinestResolvableChordKm( 256.0f ) * 1000.0;
    }

    /// The coverage `Clouds_Demo` and the other shipped cloud scenes are authored at. The calibration
    /// below is for the shipped sky, so it is measured on the shipped sky.
    constexpr float kShippedCoverage = 0.762f;

    /// What one walk of the field's columns records, so that a ladder of erosion depths costs ONE walk.
    ///
    /// It is a census and not a measurement: the profile and the erosion noise are sampled once per cell of
    /// a 48 x 48 x 240 grid and kept, and every depth is then integrated out of the same samples. Walking
    /// per depth instead would make the ladder below thirteen times more expensive and — worse — would let
    /// two rows of it disagree for a reason other than the depth.
    struct SurfaceCensus
    {
        struct Column
        {
            std::vector<float> Profile;
            std::vector<float> Noise;
        };

        std::vector<Column> Columns;
        std::vector<float>  ReferenceKm;           // where each column goes opaque with NO erosion; < 0 if never
        double              SurfaceProfile  = 0.0; // the profile at that un-eroded surface, averaged
        long                Surfaces        = 0;
        float               EnvelopeKm      = 0.0f;
        float               StepKm          = 0.0f;
        float               ExtinctionPerKm = 0.0f;

        struct Result
        {
            double TravelM;   // how far the erosion moved the visible surface, metres
            double LostShare; // share of opaque columns that stopped being opaque
            long   Opaque;
        };

        /// Where a column becomes opaque at an effective cut depth `t`, kilometres; negative if it never does.
        float SurfaceKmAt( const Column& column, float t ) const
        {
            double opticalDepth = 0.0;
            for ( int ih = static_cast<int>( column.Profile.size() ) - 1; ih >= 0; --ih )
            {
                if ( column.Profile[ih] <= 0.0f )
                    continue;

                opticalDepth += static_cast<double>( ErodedDensity( column.Profile[ih], column.Noise[ih], t ) ) *
                                ExtinctionPerKm * StepKm;

                if ( opticalDepth >= 1.0 )
                    return ( ih + 0.5f ) / static_cast<float>( column.Profile.size() ) * EnvelopeKm;
            }
            return -1.0f;
        }

        Result At( float t ) const
        {
            long   opaque = 0;
            long   lost   = 0;
            double travel = 0.0;

            for ( size_t i = 0; i < Columns.size(); ++i )
            {
                if ( ReferenceKm[i] < 0.0f )
                    continue;

                ++opaque;

                const float moved = SurfaceKmAt( Columns[i], t );
                if ( moved < 0.0f )
                {
                    ++lost;
                    continue;
                }
                travel += ( ReferenceKm[i] - moved ) * 1000.0;
            }

            return Result{ opaque > lost ? travel / static_cast<double>( opaque - lost ) : 0.0,
                           opaque > 0 ? static_cast<double>( lost ) / static_cast<double>( opaque ) : 0.0,
                           opaque };
        }
    };

    /// Walks the shipped field once. The volume it walks is baked by the SHIPPED generator, so the shape of
    /// the lump — Assets::kCloudLumpVerticalOverHorizontal — is an input to every number that comes out of
    /// here, which is the whole reason the relation test below can see it at all.
    SurfaceCensus TakeSurfaceCensus( const CloudFieldParams& params )
    {
        SurfaceCensus census;

        census.EnvelopeKm = EnvelopeThicknessKm( DefaultShape() );

        constexpr int kColumns = 48;
        constexpr int kLevels  = 240;

        census.StepKm = census.EnvelopeKm / kLevels;

        // WHAT A RAY ACTUALLY INTEGRATES, and it is a product of THREE numbers rather than one: the layer's
        // Extinction Scale, the type's own extinction factor, and the type's DENSITY factor — because the
        // density the march multiplies the extinction by is the eroded profile times that factor. Leaving
        // the density out puts the surface a decile deeper than the frame does.
        census.ExtinctionPerKm = kExtinctionPerKm * DefaultShape().ExtinctionFactor * DefaultShape().DensityFactor;

        census.Columns.reserve( kColumns * kColumns );

        for ( int iz = 0; iz < kColumns; ++iz )
            for ( int ix = 0; ix < kColumns; ++ix )
            {
                const float x = OriginKm().x + PeriodKm() * ( ix + 0.5f ) / kColumns;
                const float z = OriginKm().y + PeriodKm() * ( iz + 0.5f ) / kColumns;

                SurfaceCensus::Column column;
                column.Profile.assign( kLevels, 0.0f );
                column.Noise.assign( kLevels, 0.0f );

                for ( int ih = 0; ih < kLevels; ++ih )
                {
                    const float fraction = ( ih + 0.5f ) / kLevels;
                    const vec3  at( x, fraction * census.EnvelopeKm, z );

                    const CloudFieldSample field = SampleCloudField( params, fraction, at );
                    if ( field.Profile <= 0.0f )
                        continue;

                    column.Profile[ih] = field.Profile;
                    column.Noise[ih]   = ErosionNoiseAt( params, field, at );
                }
                census.Columns.push_back( std::move( column ) );
            }

        double sumProfileAtSurface = 0.0;

        census.ReferenceKm.reserve( census.Columns.size() );

        for ( const SurfaceCensus::Column& column : census.Columns )
        {
            census.ReferenceKm.push_back( census.SurfaceKmAt( column, 0.0f ) );

            // The profile AT that surface, which is what decides how much of the cut ever reaches the eye.
            double opticalDepth = 0.0;
            for ( int ih = kLevels - 1; ih >= 0; --ih )
            {
                if ( column.Profile[ih] <= 0.0f )
                    continue;

                opticalDepth += static_cast<double>( column.Profile[ih] ) * census.ExtinctionPerKm * census.StepKm;
                if ( opticalDepth >= 1.0 )
                {
                    sumProfileAtSurface += column.Profile[ih];
                    ++census.Surfaces;
                    break;
                }
            }
        }

        census.SurfaceProfile =
             census.Surfaces > 0 ? sumProfileAtSurface / static_cast<double>( census.Surfaces ) : 0.0;

        return census;
    }
} // namespace

TEST( CloudFieldErosion, TheMirrorOfTheErosionAgreesWithTheShader )
{
    // The two tests after this one sweep the erosion over ONE walk of the field, which is only legitimate
    // while ErosionNoiseAt is the shader's own expression. This is the line that fails the day the
    // composite in Common/CloudField.glslh changes and the mirror does not.
    //
    // IT SWEEPS THE DETAIL CHARACTER, AND THAT IS NOT THOROUGHNESS — IT IS A HOLE THAT WAS FOUND BY
    // BREAKING IT. The composite mixes a WISPY pair and a BILLOWY pair and then blends the two on
    // DetailType. The built-in congestus this suite's fixture is built on has a DetailCharacter of 1.00,
    // which is the billowy end EXACTLY: at that value the wispy pair's own frequency blend multiplies out
    // of the expression entirely. Reversing `mix(noise.x, noise.y, Profile)` to
    // `mix(noise.y, noise.x, Profile)` in the shader left this whole suite GREEN — two of the volume's
    // four channels were never read by any assertion in the programme. Sweeping the character is what
    // makes the mirror a mirror of the composite rather than of half of it.
    CloudFieldParams params = ParamsAtCoverage( kShippedCoverage );
    params.DetailStrength   = 1.0f;

    const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    int    checked = 0;
    double worst   = 0.0;

    for ( const float character : { 0.0f, 0.35f, 0.7f, 1.0f } )
        for ( int iz = 0; iz < 12; ++iz )
            for ( int ix = 0; ix < 12; ++ix )
                for ( int ih = 0; ih < 8; ++ih )
                {
                    const float fraction = ( ih + 0.5f ) / 8.0f;
                    const vec3  at( OriginKm().x + PeriodKm() * ( ix + 0.5f ) / 12.0f, fraction * envelopeKm,
                                    OriginKm().y + PeriodKm() * ( iz + 0.5f ) / 12.0f );

                    CloudFieldSample field = SampleCloudField( params, fraction, at );
                    if ( field.Profile <= 0.0f )
                        continue;

                    field.DensityScale = 1.0f;
                    field.DetailFactor = 1.0f;
                    field.DetailType   = character;

                    const float fromShader = CloudSampleDensity( params, field, at );
                    const float fromMirror =
                         ErodedDensity( field.Profile, ErosionNoiseAt( params, field, at ), 1.0f );

                    worst = std::max( worst, static_cast<double>( std::abs( fromShader - fromMirror ) ) );
                    ++checked;
                }

    std::printf( "[CloudFieldErosion] mirror checked at %d samples, worst disagreement %.3e\n", checked, worst );

    EXPECT_GT( checked, 100 ) << "the fixture put no cloud in front of the mirror, so it checked nothing";
    EXPECT_LT( worst, 1e-6 ) << "the erosion mirror and Common/CloudField.glslh disagree, so every sweep "
                                "below is a sweep of something else";
}

TEST( CloudFieldSpecies, TheWinningSpeciesEdgeIsCutFromItsOwnNoiseVolume )
{
    // THE PROGRAMME'S LAST RECORDED DEBT, as an assertion. Until phase NV there was ONE `sampler3D` on the
    // march and the whole layer was eroded by the volume of whichever slot happened to be filled first —
    // so a Cirrus standing beside a Cumulus was cut from the cumulus' noise, silently, while the Cloud
    // Type panel's own tooltip promised the opposite. It was measured on the frame before it was fixed:
    // changing the cirrus type's NoiseVolume moved 0 pixels of 980 480 at all six protocol points when it
    // sat in the second slot, and between 50.5 % and 72.6 % of them when it sat in the first
    // (Docs/Clouds/CALIBRATION.md section NV).
    //
    // WHAT MAKES THIS AN ASSERTION AND NOT A TAUTOLOGY: neither side is the shader's own arithmetic. The
    // left-hand side is CloudSampleDensity — the seam, compiled as C++ — and the right-hand side is the
    // erosion mirror built on the volume the test names DIRECTLY, without going through the sample's
    // NoiseSlot at all. They can only agree if the slot travelled with the winner.
    //
    // AND IT SWEEPS BOTH DIRECTIONS OF THE PAIR, which is not thoroughness either. A shader that ignored
    // the slot and always read volume 0 passes any test whose species-0 volume is the interesting one; a
    // shader that always read the LAST slot passes the mirror image of it. Only asking about both species
    // in one sky can separate "the winner's volume" from either constant.
    CloudFieldParams params = TwoSpeciesParams( 0.55f );

    // Species 1 — the tower — is given the shipped fine-wisp volume in slot 1, exactly as an artist gives
    // a type its own `.dcnv`. Species 0 keeps the default in slot 0.
    CloudBindSpeciesNoise( params, 1, 1, kFineWispPeriods );

    const float altitudeKm = 2.40f;
    const float fraction   = FractionOfSetEnvelope( altitudeKm );

    int checked        = 0;
    int slotWrong      = 0;
    int densityWrong   = 0;
    int wouldHaveMoved = 0;

    double worstDisagreement = 0.0;

    constexpr int kColumns = 96;

    for ( int iz = 0; iz < kColumns; ++iz )
    {
        for ( int ix = 0; ix < kColumns; ++ix )
        {
            const vec3 position( OriginKm().x + PeriodKm() * ( ix + 0.5f ) / kColumns,
                                 altitudeKm - kDeck.BaseAltitudeKm,
                                 OriginKm().y + PeriodKm() * ( iz + 0.5f ) / kColumns );

            const float deck  = SpeciesProfileAt( params, 0, fraction, position );
            const float tower = SpeciesProfileAt( params, 1, fraction, position );

            const CloudFieldSample sample = SampleCloudField( params, fraction, position );
            if ( sample.Profile <= 0.0f )
                continue;

            ++checked;

            // `>=` for the reason the union test next door gives: the volume is quantised to a 255th, ties
            // happen, and the producer takes a species only when it STRICTLY exceeds the best so far — so
            // a tie is won by the EARLIER slot.
            const int expectedSlot = deck >= tower ? 0 : 1;
            if ( sample.NoiseSlot != expectedSlot )
                ++slotWrong;

            // THE DENSITY, computed twice: once by the seam, and once by the mirror told which volume to
            // read rather than asked. ErosionNoiseAt reads sample.NoiseSlot, so the mirror is handed a
            // COPY whose slot is the one this test believes in — which is what makes a disagreement a
            // statement about the seam and not about the mirror.
            CloudFieldSample believed = sample;
            believed.NoiseSlot        = expectedSlot;

            const float fromSeam = CloudSampleDensity( params, sample, position );
            const float fromMirror =
                 ErodedDensity( sample.Profile, ErosionNoiseAt( params, believed, position ),
                                std::clamp( params.DetailStrength * sample.DetailFactor, 0.0f, 1.0f ) );

            const double gap  = std::abs( static_cast<double>( fromSeam ) -
                                          static_cast<double>( fromMirror ) * sample.DensityScale );
            worstDisagreement = std::max( worstDisagreement, gap );
            if ( gap > 1e-6 )
                ++densityWrong;

            // AND THE OTHER VOLUME WOULD HAVE GIVEN A DIFFERENT ANSWER HERE. Without this the whole test
            // could pass on a sky where the two volumes happen to agree, which is the shape a green
            // sabotage takes: the two `.dcnv` files differ by a factor of two on every lattice period, so
            // they must disagree somewhere, and counting where says the assertions above had something to
            // catch.
            CloudFieldSample other = sample;
            other.NoiseSlot        = expectedSlot == 0 ? 1 : 0;
            if ( std::abs( CloudSampleDensity( params, other, position ) - fromSeam ) > 1e-6f )
                ++wouldHaveMoved;
        }
    }

    std::printf( "[CloudFieldSpecies] %d samples carried a species; %d would have moved on the other "
                 "volume; worst density disagreement %.3e\n",
                 checked, wouldHaveMoved, worstDisagreement );

    CloudNoiseVolumeResetAll();

    EXPECT_GT( checked, 100 ) << "no sample carried a species, so nothing about the volumes was checked";
    EXPECT_EQ( slotWrong, 0 ) << "the sample did not carry the WINNING species' noise slot";
    EXPECT_EQ( densityWrong, 0 ) << "the erosion was not cut from the volume the winner names";
    EXPECT_GT( wouldHaveMoved, checked / 10 )
         << "the two volumes agree almost everywhere on this fixture, so the assertions above could not "
            "have failed and this test is inert";
}

TEST( CloudFieldErosion, TheBoundOnAMaterialFactorIsTheIdentityEverywhereTheFactorIsLegal )
{
    // THE OTHER HALF OF CLOUD_MATERIAL_FACTOR_MAX, and it is what stops that bound from being a licence to
    // clamp anything to anything. The ceiling is 8 because 8 is the number Assets::ValidateCloudTypeShape
    // already refuses to load a cloud type past, so across the whole legal range the bound has to be the
    // IDENTITY. A guard that quietly moved a shipped sky would be a worse defect than the NaN it removes,
    // and it would move it in the direction nobody looks — slightly, everywhere, with no error anywhere.
    //
    // WHAT MAKES THIS AN ASSERTION RATHER THAN A TAUTOLOGY: the right-hand side is ErodedDensity, the
    // mirror the test above pins to the shader, and the mirror has NO bound in it. It takes the effective
    // depth `clamp(strength * factor, 0, 1)` as an argument and computes the remap from first principles.
    // So this compares the bounded shader against the unbounded formula, which is exactly the claim.
    CloudFieldParams params = ParamsAtCoverage( kShippedCoverage );

    const vec3 at( 1.7f, 0.9f, 2.3f );

    int checked = 0;

    for ( const float strength : { 0.0f, 0.1f, 0.4f, 1.0f } )
    {
        for ( const float factor : { 0.0f, 0.25f, 1.0f, 2.5f, 8.0f } )
        {
            params.DetailStrength = strength;

            CloudFieldSample field;
            field.Profile          = 0.4f;
            field.DetailType       = DefaultShape().DetailCharacter;
            field.DensityScale     = 1.0f;
            field.DetailFactor     = factor;
            field.ExtinctionFactor = 1.0f;

            const float fromShader = CloudSampleDensity( params, field, at );
            const float fromMirror = ErodedDensity( field.Profile, ErosionNoiseAt( params, field, at ),
                                                    glm::clamp( strength * factor, 0.0f, 1.0f ) );

            EXPECT_NEAR( fromShader, fromMirror, 1e-6f )
                 << "strength " << strength << " x factor " << factor
                 << ": the bound altered a value inside the range Assets::ValidateCloudTypeShape allows";
            ++checked;
        }
    }

    EXPECT_EQ( checked, 20 );
}

TEST( CloudFieldErosion, TheErosionsWaveIsShorterThanABodyAndCoarserThanTheMarchsOwnChord )
{
    // THE RELATION, AND IT IS TWO-SIDED.
    //
    //   ABOVE, by the body. An erosion whose wavelength is as long as a cloud has ONE wave across that
    //   cloud and cannot texture it — it scales it. This is what shipped for two phases: 884 m of wave
    //   against a 1071 m chord, and it is why no setting of Detail Strength ever produced an edge.
    //
    //   BELOW, by the march. Structure finer than CloudFinestResolvableChordKm is structure whose
    //   sampling is decided by the ray's jitter, which is the definition of dither in this programme. At
    //   half a kilometre of tile the wave is 119 m against a 125 m chord and is already past it.
    //
    // The window between the two is narrow and the shipped tile sits in it. If this fails, either the
    // generator's bodies changed size or the tile was moved out of the window.
    CloudFieldParams params     = ParamsAtCoverage( kShippedCoverage );
    const float      envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    const float stepKm = 0.015625f; // a quarter of the march's 62.5 m search step

    std::vector<float> bodyChords;
    std::vector<float> erosionHalfWaves;

    constexpr int kLines = 64;

    for ( int line = 0; line < kLines; ++line )
    {
        const float z        = OriginKm().y + PeriodKm() * ( line + 0.5f ) / kLines;
        const float fraction = 0.25f + 0.5f * ( ( line * 7 ) % kLines ) / static_cast<float>( kLines );
        const float y        = fraction * envelopeKm;

        float bodyRun    = 0.0f;
        float erosionRun = 0.0f;
        bool  wasBody    = false;
        bool  wasHigh    = false;

        for ( float x = OriginKm().x; x < OriginKm().x + PeriodKm(); x += stepKm )
        {
            const CloudFieldSample field = SampleCloudField( params, fraction, vec3( x, y, z ) );

            const bool body = field.Profile > 0.5f;
            if ( body )
                bodyRun += stepKm;
            else if ( wasBody )
            {
                bodyChords.push_back( bodyRun );
                bodyRun = 0.0f;
            }
            wasBody = body;

            // The erosion field is defined everywhere, so it is walked on its own terms: a run above its
            // own midpoint is one half-wave. It is read at the depth the eye sees a cloud at, because the
            // composite blends its two octaves ON the profile and the wavelength therefore depends on it.
            CloudFieldSample probe = field;
            probe.Profile          = 0.694f;
            probe.DetailType       = DefaultShape().DetailCharacter;

            const bool high = ErosionNoiseAt( params, probe, vec3( x, y, z ) ) > 0.5f;
            if ( high )
                erosionRun += stepKm;
            else if ( wasHigh )
            {
                erosionHalfWaves.push_back( erosionRun );
                erosionRun = 0.0f;
            }
            wasHigh = high;
        }
    }

    auto meanM = []( const std::vector<float>& values )
    {
        double sum = 0.0;
        for ( float value : values )
            sum += value;
        return values.empty() ? 0.0 : sum / values.size() * 1000.0;
    };

    ASSERT_FALSE( bodyChords.empty() ) << "the fixture put no bodies in the volume to measure";
    ASSERT_FALSE( erosionHalfWaves.empty() ) << "the erosion field never crossed its own midpoint";

    const double bodyM = meanM( bodyChords );
    const double waveM = 2.0 * meanM( erosionHalfWaves );

    std::printf( "[CloudFieldErosion] tile %.2f km: erosion wave %.0f m against a body chord of %.0f m "
                 "(%.2f of it)\n",
                 params.DetailTileKm, waveM, bodyM, waveM / bodyM );

    EXPECT_LE( waveM, 0.5 * bodyM )
         << "the erosion's wave is " << waveM << " m against a body chord of " << bodyM
         << " m, so there is not even one full wave across half a cloud — the erosion scales the body "
            "instead of texturing it, and no setting of Detail Strength can produce an edge";

    // AND THE LOWER END AT EVERY QUALITY TIER, stated as a loop rather than assumed.
    //
    // Graphic::CloudQualityScale carries NO Max Steps field — phase Э3 measured that halving it would put
    // five of nine shipped types past Nyquist and refused — so all four tiers march with the component's
    // own count and this bound is the same at every one of them. The four literals below say that out
    // loud, exactly as Desert/Tests/Engine/CloudProceduralField says it about the lumps: the day a tier
    // gains a step count, one of these lines fails, and it should, because the erosion's wave does not
    // move when the march's chord does.
    for ( const float maxSteps : { 256.0f, 256.0f, 256.0f, 256.0f } )
    {
        const double tierChordM = Desert::Graphic::CloudFinestResolvableChordKm( maxSteps ) * 1000.0;

        EXPECT_GE( waveM, tierChordM )
             << "at Max Steps " << maxSteps << " the erosion's wave is " << waveM << " m against the "
             << tierChordM
             << " m the march can be relied on to find, so whether a wisp is sampled at all is decided by "
                "the ray's jitter and it reads as dither";
    }

    std::printf( "[CloudFieldErosion] at every tier the march resolves %.0f m and the erosion's wave is "
                 "%.2fx it\n",
                 MarchResolvableM(), waveM / MarchResolvableM() );
}

TEST( CloudFieldErosion, TheShippedStrengthMovesTheSurfaceTheEyeSeesWithoutEatingTheBody )
{
    // MEASURED, NOT DERIVED. For every column: the altitude at which the optical depth of a ray coming up
    // from below first reaches 1 — the depth at which the cloud becomes opaque, which is where the eye
    // puts its surface — with the erosion on and with it off. The DIFFERENCE is what the erosion buys.
    //
    // THE TWO ENDS OF THE ANSWER:
    //
    //   IT HAS TO MOVE THE SURFACE BY MORE THAN THE MARCH CAN RESOLVE, or the structure it carves is
    //   below the scale the renderer represents and the setting is a fetch that changes nothing. At the
    //   0.10 this replaces the travel was 54 m against a 125 m chord — under half of it.
    //
    //   AND IT MUST NOT DISSOLVE THE LAYER. A column that was opaque and stops being opaque is a hole in
    //   the deck; that is the "translucent veil" this parameter was first lowered to escape.
    //
    // A NUMBER THIS TEST PRINTS AND DOES NOT ASSERT, because the next calibration needs it: the surface
    // sits at a PROFILE of about 0.69, so the erosion's own (1 - Profile) weight throttles the cut to
    // about 31 % of its nominal depth exactly where the eye is looking. That ceiling is why no setting of
    // this slider produces a shredded silhouette, it predates phase Э5, and moving it is a design change
    // rather than a calibration.
    const CloudFieldParams params = ParamsAtCoverage( kShippedCoverage );
    const SurfaceCensus    census = TakeSurfaceCensus( params );

    ASSERT_GT( census.Surfaces, 0 )
         << "no column in the fixture ever became opaque, so there is no surface to move";

    const Desert::ECS::VolumetricCloudData shipped;

    const auto shippedResult = census.At( shipped.DetailStrength );

    ASSERT_GT( shippedResult.Opaque, 0 ) << "no column was opaque without the erosion";

    const double meanTravelM = shippedResult.TravelM;
    const double lostShare   = shippedResult.LostShare;

    std::printf( "[CloudFieldErosion] the surface sits at profile %.3f; at strength %.2f it travels %.0f m "
                 "(the march resolves %.0f m) and %.3f of the opaque columns stop being opaque\n",
                 census.SurfaceProfile, shipped.DetailStrength, meanTravelM, MarchResolvableM(), lostShare );

    EXPECT_GE( meanTravelM, MarchResolvableM() )
         << "at the shipped Detail Strength the erosion moves the surface the eye sees by " << meanTravelM
         << " m, which is under the " << MarchResolvableM()
         << " m the march can resolve — the cut is finer than the renderer can represent, so it costs a "
            "fetch and changes nothing a viewer can see";

    EXPECT_LE( lostShare, 0.15 ) << "the erosion dissolved " << lostShare * 100.0
                                 << "% of the columns that were opaque without it, which is the "
                                    "translucent veil this parameter was first lowered to escape";

    // WHERE THE FLOOR ACTUALLY IS, printed so that the shipped value can be read against it rather than
    // taken on trust. This is the sweep the calibration was chosen from.
    std::printf( "[CloudFieldErosion]  strength   travel (m)   dissolved\n" );
    for ( const float t :
          { 0.10f, 0.20f, 0.30f, 0.35f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f, 0.65f, 0.70f, 0.80f, 1.00f } )
    {
        const auto row = census.At( t );
        std::printf( "[CloudFieldErosion]    %.2f       %6.1f       %.3f\n", t, row.TravelM, row.LostShare );
    }

    // AND THE OTHER END OF THE SHIPPED VALUE, which is otherwise a number nobody can check: the floor
    // above is cleared by 0.35 and by 1.00 alike, and every step above it costs cloud for a gain nothing
    // has measured a need for.
    //
    // THIS BOUND IS A CONVENTION AND IS LABELLED AS ONE, because the honest answer is that the floor does
    // not fix the value on its own. The measured floor is 125 m of travel; 0.35 clears it by ONE metre and
    // 0.40 by fourteen, and a default is set with headroom over its floor rather than balanced on it — a
    // one-per-cent margin would make this suite fail on any change to the generator that moved a body by a
    // voxel. So the shipped value is the first step with real headroom, and what is asserted is that it
    // stays within an octave of its own floor. A sabotage that raises Detail Strength and changes nothing
    // else is caught here and nowhere else.
    EXPECT_LE( meanTravelM, 2.0 * MarchResolvableM() )
         << "the shipped Detail Strength moves the surface by " << meanTravelM << " m, more than twice the "
         << MarchResolvableM()
         << " m floor that chose it — the value has drifted a long way above the bound that justifies it, "
            "and every step of that costs cloud for a gain nothing has measured";
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════
// THE SHAPE OF A LUMP AND THE DEPTH OF THE EROSION ARE ONE CALIBRATION, AND THIS IS WHERE THEY MEET.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════
//
// THE DEFECT THIS EXISTS FOR, and it is a defect of the repository rather than of the sky. Two numbers live
// in two files and neither names the other:
//
//     Assets::kCloudLumpVerticalOverHorizontal   the shape of the lump a cloud is built out of
//     ECS::VolumetricCloudData::DetailStrength   how deeply the erosion cuts into it
//
// They are not independent. A taller lump packs more density into a metre of ray, so the altitude at which
// the optical depth first reaches 1 — the surface the eye puts the cloud at — sits at a SHALLOWER profile
// (0.632 at a lump of 0.45, 0.576 at one of 0.75), and a given cut moves that surface a SHORTER distance.
// Detail Strength is fixed from below by a floor on exactly that distance: the chord the march can be
// relied on to find. So raising the lump lowers the erosion's headroom, and past a point it pushes it
// through the floor.
//
// HOW IT WAS FOUND, because it is the argument for this test being here at all. §SIL raised the lump from
// 0.45 to 0.75, measured the sky, shot the frames, wrote the report and COMMITTED it. The travel had gone
// to 101 m against a 125 m floor. Nothing in the repository said so until a full sweep of every suite —
// run for an unrelated reason — turned this file red, and the report's own note says it "would have
// shipped". §SIL then backed the lump down to 0.45, which is the largest value that clears a floor nobody
// had connected it to, and recorded the coupling in prose.
//
// PROSE IS NOT A RELATION. §2.3.1 of the contract is about exactly this shape — two values that must agree
// with nothing asserting that they do — and the fix is not to widen the floor or to freeze either number,
// it is to assert the AGREEMENT. This test reads both symbols, bakes the volume the pair actually
// produces, and measures what the pair delivers. Move either one alone and it is red, and the message
// names the other one.
//
// WHY THE THRESHOLD IS 1.05x AND NOT THE 1.11x THE PAIR SHIPS AT. The physical bound is 1.00x — below it
// the erosion carves structure finer than the renderer can represent. §DS's convention is to ship the
// first ladder step with REAL headroom over that, which it put at 1.11x and which this pair also lands on
// (0.65 gives 139 m against 125). Asserting at the shipped value would make the test a copy of the default
// rather than a guard on it, and asserting at the bare floor would let a future pair sit balanced on a
// bound §DS refused to balance on by name. 1.05x is halfway between the bound being protected and the
// value protecting it: a generator change that moves a body by a voxel does not trip it, and a calibration
// that quietly gives up its headroom does.
TEST( CloudFieldErosion, TheLumpsAspectAndTheErosionsStrengthAreOneCalibrationAndNotTwoNumbers )
{
    const Desert::ECS::VolumetricCloudData shipped;

    const float  aspect   = Desert::Assets::kCloudLumpVerticalOverHorizontal;
    const float  strength = shipped.DetailStrength;
    const double floorM   = MarchResolvableM();

    const CloudFieldParams params = ParamsAtCoverage( kShippedCoverage );
    const SurfaceCensus    census = TakeSurfaceCensus( params );

    ASSERT_GT( census.Surfaces, 0 ) << "no column in the fixture ever became opaque, so there is nothing to "
                                       "measure and this test asserted nothing";

    const auto result = census.At( strength );

    ASSERT_GT( result.Opaque, 0 ) << "no column was opaque without the erosion";

    std::printf( "[CloudFieldErosion] lump aspect %.3f x strength %.3f: the surface sits at profile %.3f "
                 "and travels %.1f m, %.2fx the %.0f m the march resolves\n",
                 aspect, strength, census.SurfaceProfile, result.TravelM, result.TravelM / floorM, floorM );

    // ── THE RELATION, AND IT IS A WINDOW RATHER THAN A FLOOR ────────────────────────────────────────────
    //
    // THE TOP OF THE WINDOW IS NOT DECORATION, AND MEASURING IT IS WHAT SHOWED WHY. The test above ships
    // §DS's own ceiling — the travel must stay within an OCTAVE of its floor — whose stated job is to catch
    // "somebody raised Detail Strength and changed nothing else". That bound still fires, but no longer for
    // that case: it fires when the LUMP is lowered without lowering the strength with it, because a flatter
    // lump is optically thinner per metre and the same cut travels FURTHER. Against the 0.75 lump the top
    // of the strength slider now travels 180 m — 1.44x the floor, comfortably inside an octave — so the
    // octave has stopped catching the drift it was written for. Its bite was an accident of the aspect it
    // was measured at, and this is the half of the window that restores it.
    //
    // 1.35x IS WHERE IT IS BECAUSE BOTH ENDS ARE MEASURED. The shipped pair sits at 1.11x, the top of the
    // slider at 1.44x: a ceiling anywhere between those two catches a slider dragged to maximum without
    // being balanced on the shipped value, and 1.35 leaves the shipped pair a fifth of headroom below it
    // while leaving the slider's top a fifteenth above.
    constexpr double kRequiredHeadroom = 1.05;
    constexpr double kHeadroomCeiling  = 1.35;

    EXPECT_LE( result.TravelM, kHeadroomCeiling * floorM )
         << "THE PAIR HAS DRIFTED UPWARD. A lump aspect of " << aspect << " against a Detail Strength of "
         << strength << " moves the visible surface " << result.TravelM << " m, " << result.TravelM / floorM
         << "x the " << floorM << " m the march resolves, and past the " << kHeadroomCeiling
         << "x that the pair is calibrated to hold.\n"
            "Every step above the floor costs cloud for a gain nothing has measured a need for. Either the "
            "strength was raised on its own — at the shipped aspect the top of the slider reaches 1.44x — "
            "or the lump was made FLATTER without the strength coming down with it, which is the same "
            "calibration coming apart in the other direction.";

    EXPECT_GE( result.TravelM, kRequiredHeadroom * floorM )
         << "THE LUMP AND THE EROSION HAVE COME APART. A lump aspect of " << aspect
         << " (Assets::kCloudLumpVerticalOverHorizontal) against a Detail Strength of " << strength
         << " (ECS::VolumetricCloudData) moves the visible surface " << result.TravelM << " m, which is "
         << result.TravelM / floorM << "x the " << floorM << " m the march can be relied on to find — under the "
         << kRequiredHeadroom
         << "x this pair is calibrated to hold.\n"
            "These two numbers are ONE calibration: a taller lump is optically thicker per metre, so the "
            "same cut moves the surface less far. If the aspect was just raised, the strength has to "
            "follow it up; if the strength was just lowered, the aspect has to come down with it. "
            "Docs/Clouds/CALIBRATION.md §SIL2 carries the measured ladder for both.";

    // ── AND THE LEVER HAS TO STILL BE A LEVER ───────────────────────────────────────────────────────────
    //
    // The repair above only works while a deeper cut buys more travel. That is not a tautology of the
    // maths: the erosion's weight is `(1 - Profile)` and the remap it feeds is a ratio, so a field whose
    // surface sat deep enough could in principle stop responding — and if it ever did, "raise Detail
    // Strength to pay for a taller lump" would be advice that quietly does nothing. Asserting the
    // MONOTONICITY is what makes the recipe in both files a recipe rather than a hope.
    double previousM = -1.0;
    for ( const float t : { 0.20f, 0.40f, 0.60f, 0.80f, 1.00f } )
    {
        const double travelM = census.At( t ).TravelM;

        EXPECT_GT( travelM, previousM )
             << "at an effective cut of " << t << " the surface travels " << travelM << " m, no further than the "
             << previousM
             << " m a shallower cut moved it — the erosion has stopped being the lever that pays for the "
                "lump's shape, so the coupling recorded in CloudProceduralVolume.hpp and "
                "VolumetricCloudComponent.hpp cannot be repaired the way both files say it can";

        previousM = travelM;
    }

    // ── AND THE PAIR MUST NOT DISSOLVE THE DECK EITHER ──────────────────────────────────────────────────
    //
    // The other end of the same repair. Paying for a taller lump with a deeper cut is only legitimate while
    // the cut is shredding the deck's edge rather than opening holes through it, and the two are told apart
    // by exactly one number: how many columns that were opaque stop being opaque. The bound is the one the
    // test above ships with, restated here against the PAIR so that a future aspect paid for with a very
    // deep cut is caught by the thing that makes it wrong.
    EXPECT_LE( result.LostShare, 0.15 )
         << "the pair dissolves " << result.LostShare * 100.0
         << "% of the columns that were opaque without the erosion — the strength bought to pay for a lump "
            "aspect of "
         << aspect
         << " is deep enough to put holes through the deck, which is the translucent veil this "
            "parameter was first lowered to escape";
}

// ---------------------------------------------------------------------------------------------------
// WHAT THE EYE SEES AGAINST WHAT THE EROSION CUTS — task Р9, 2026-08-28
// ---------------------------------------------------------------------------------------------------
//
// WHY THIS SECTION EXISTS. §DS above measures how far the erosion moves the visible surface, and its
// answer — 138.7 m at the shipped strength — is a MEAN. A mean displacement is a cloud of a slightly
// different SIZE; it is not a rough one. Nothing in this suite, or anywhere else in the programme, had
// measured the quantity the owner's complaint is actually about: how much the surface VARIES.
//
// Measured here, the answer is that the erosion contributes about 4.9 m of roughness at a 50 m lag on top
// of the 56.1 m the bare lumps already have — under a tenth — and that the whole remaining range of every
// knob aimed at it is worth a few metres more. The reason is a ratio of two lengths, and it is the finding
// this section exists to pin:
//
//     the depth the eye looks through before the cloud is opaque   ~635 m
//     the distance over which the erosion field decorrelates       ~200 m
//
// THE SILHOUETTE IS A LINE INTEGRAL, not a surface. A ray does not stop where the density becomes
// non-zero; it stops where it has accumulated unit optical depth, and on the shipped sky that takes 635 m
// of cloud. Integrating over 635 m of a field that decorrelates in 200 m is a LOW-PASS FILTER, and only
// the long-wavelength part of the erosion survives it — which is precisely the part that reads as a change
// of size rather than as detail.
//
// THAT RATIO EXPLAINS FOUR REFUTATIONS THAT WERE PREVIOUSLY UNRELATED (Docs/Clouds/DIAGNOSIS_CARTOON.md):
// the whole DetailStrength range is worth 2.86/255 because it scales an amplitude that is averaged away
// afterwards; DetailTileSize is worth 1.29/255 because a coarser tile survives the averaging and is too
// coarse to read as detail while a finer one reads as detail and does not survive, and the two cancel;
// and MaxSteps and the trace resolution buy nothing because they are about SAMPLING where this is about
// AVERAGING (Р6's octave, 1.3-3.4 % for 2.83x the cost).
//
// WHAT WOULD MOVE IT is the ratio's numerator or its denominator, and both are priced in Р9's report. The
// assertions below are the two halves of the relation, so that a change to either is noticed here.
namespace
{
    struct SurfaceRoughness
    {
        double PenetrationM   = 0.0;         // how deep the ray goes before the cloud is opaque
        double DecorrelationM = 0.0;         // the lag at which the erosion field is 95 % of its far-field spread
        double BareM[3]       = { 0, 0, 0 }; // roughness of the silhouette with the erosion OFF
        double ErodedM[3]     = { 0, 0, 0 }; // and with it at the shipped strength
    };

    /// Walks a patch of the SHIPPED field and measures the silhouette's roughness at three lags, with the
    /// erosion off and on. Sized for Debug: 64 x 64 columns at 80 m over 240 levels is under a million
    /// samples, which is the same order as the census above.
    SurfaceRoughness TakeSurfaceRoughness()
    {
        const CloudFieldParams params = ParamsAtCoverage( kShippedCoverage );

        constexpr int   kN         = 64;
        constexpr float kSpacingKm = 0.08f;
        constexpr int   kLevels    = 240;

        const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );
        const float stepKm     = envelopeKm / kLevels;
        const float extPerKm   = kExtinctionPerKm * DefaultShape().ExtinctionFactor * DefaultShape().DensityFactor;

        const float x0 = OriginKm().x + 0.5f * PeriodKm() - 0.5f * kN * kSpacingKm;
        const float z0 = OriginKm().y + 0.5f * PeriodKm() - 0.5f * kN * kSpacingKm;

        std::vector<float> profile( static_cast<size_t>( kN ) * kN * kLevels, 0.0f );
        std::vector<float> noise( static_cast<size_t>( kN ) * kN * kLevels, 0.0f );

        for ( int iz = 0; iz < kN; ++iz )
            for ( int ix = 0; ix < kN; ++ix )
                for ( int ih = 0; ih < kLevels; ++ih )
                {
                    const float fraction = ( ih + 0.5f ) / kLevels;
                    const vec3  at( x0 + kSpacingKm * ( ix + 0.5f ), fraction * envelopeKm,
                                    z0 + kSpacingKm * ( iz + 0.5f ) );

                    const CloudFieldSample field = SampleCloudField( params, fraction, at );
                    if ( field.Profile <= 0.0f )
                        continue;

                    const size_t k = ( static_cast<size_t>( iz ) * kN + ix ) * kLevels + ih;
                    profile[k]     = field.Profile;
                    noise[k]       = ErosionNoiseAt( params, field, at );
                }

        SurfaceRoughness out;

        // The two silhouettes, and the erosion field sampled on the un-eroded one.
        std::vector<double> bare( static_cast<size_t>( kN ) * kN, -1.0 );
        std::vector<double> eroded( static_cast<size_t>( kN ) * kN, -1.0 );
        std::vector<float>  atSurface( static_cast<size_t>( kN ) * kN, -1.0f );

        const float shipped = glm::clamp( Desert::ECS::VolumetricCloudData{}.DetailStrength, 0.0f, 1.0f );

        double penetration = 0.0;
        long   surfaces    = 0;

        for ( int iz = 0; iz < kN; ++iz )
            for ( int ix = 0; ix < kN; ++ix )
            {
                const size_t cell = static_cast<size_t>( iz ) * kN + ix;
                const size_t base = cell * kLevels;

                for ( int pass = 0; pass < 2; ++pass )
                {
                    const float t            = pass == 0 ? 0.0f : shipped;
                    double      opticalDepth = 0.0;
                    int         top          = -1;

                    for ( int ih = kLevels - 1; ih >= 0; --ih )
                    {
                        if ( profile[base + ih] <= 0.0f )
                            continue;
                        if ( top < 0 )
                            top = ih;

                        opticalDepth +=
                             static_cast<double>( ErodedDensity( profile[base + ih], noise[base + ih], t ) ) *
                             extPerKm * stepKm;

                        if ( opticalDepth >= 1.0 )
                        {
                            const double km = ( ih + 0.5 ) / kLevels * envelopeKm;
                            if ( pass == 0 )
                            {
                                bare[cell]      = km * 1000.0;
                                atSurface[cell] = noise[base + ih];
                                penetration += ( top - ih ) * stepKm * 1000.0;
                                ++surfaces;
                            }
                            else
                                eroded[cell] = km * 1000.0;
                            break;
                        }
                    }
                }
            }

        out.PenetrationM = surfaces > 0 ? penetration / surfaces : 0.0;

        // Mean |f(p) - f(p + lag)| along x, for a field addressed on the grid. Used for both the surface
        // (in metres) and the erosion field (in field units).
        auto structureAt = [&]( const auto& field, int lag, double missing ) -> double
        {
            double total = 0.0;
            long   pairs = 0;
            for ( int iz = 0; iz < kN; ++iz )
                for ( int ix = 0; ix + lag < kN; ++ix )
                {
                    const double a = field[static_cast<size_t>( iz ) * kN + ix];
                    const double b = field[static_cast<size_t>( iz ) * kN + ix + lag];
                    if ( a <= missing || b <= missing )
                        continue;
                    total += std::fabs( a - b );
                    ++pairs;
                }
            return pairs > 0 ? total / pairs : 0.0;
        };

        for ( int li = 0; li < 3; ++li )
        {
            const int lag   = 1 << li; // 80, 160, 320 m
            out.BareM[li]   = structureAt( bare, lag, 0.0 );
            out.ErodedM[li] = structureAt( eroded, lag, 0.0 );
        }

        // WHERE THE EROSION FIELD STOPS BEING CORRELATED WITH ITSELF. Its structure function rises and then
        // flattens; the flat value is the far field, and the decorrelation length is the first lag that
        // reaches 95 % of it. Reported in metres so it can be compared with the penetration directly.
        const double farField = structureAt( atSurface, kN / 2, 0.0f );
        out.DecorrelationM    = kN / 2 * kSpacingKm * 1000.0;
        for ( int lag = 1; lag <= kN / 2; ++lag )
            if ( farField > 0.0 && structureAt( atSurface, lag, 0.0f ) >= 0.95 * farField )
            {
                out.DecorrelationM = lag * kSpacingKm * 1000.0;
                break;
            }

        return out;
    }
} // namespace

TEST( CloudFieldErosion, TheEyeLooksThroughMoreCloudThanTheErosionVariesOver )
{
    const SurfaceRoughness r = TakeSurfaceRoughness();

    std::printf( "[CloudFieldErosion] the eye looks through %.0f m of cloud; the erosion decorrelates in "
                 "%.0f m (%.2fx)\n",
                 r.PenetrationM, r.DecorrelationM, r.PenetrationM / std::max( r.DecorrelationM, 1.0 ) );
    std::printf( "[CloudFieldErosion]   lag    bare    eroded    added   [metres of silhouette]\n" );
    for ( int li = 0; li < 3; ++li )
        std::printf( "[CloudFieldErosion]  %4d   %6.1f   %6.1f   %6.1f\n", 80 << li, r.BareM[li], r.ErodedM[li],
                     r.ErodedM[li] - r.BareM[li] );

    // ── THE EROSION IS CONNECTED AT ALL ─────────────────────────────────────────────────────────────────
    //
    // The weakest half of the relation and the one worth having as a regression guard: whatever the ceiling
    // is, the erosion must ROUGHEN the silhouette rather than merely translate it. A refactor that left the
    // cut applied uniformly — or that lost the noise fetch and kept the mean — would leave §DS's travel
    // measurement intact and would be caught only here.
    EXPECT_GT( r.ErodedM[0] - r.BareM[0], 1.0 )
         << "the erosion moves the surface " << r.ErodedM[0] - r.BareM[0]
         << " m at an 80 m lag, so it is displacing the silhouette without roughening it — which is a cloud "
            "of a different size rather than a cloud with a surface";

    // ── AND THE CEILING IS THE RATIO, NAMED SO THAT MOVING IT IS NOTICED ────────────────────────────────
    //
    // This is the number Р9 was asked for and it is asserted rather than only printed, because the whole
    // point of the finding is that it is the binding constraint: while the eye looks through several times
    // the distance the erosion varies over, no amount of erosion can put detail on the silhouette. The
    // bound is loose on purpose — it is not a target, it is a tripwire. A change that shortens the
    // penetration (a higher extinction, a steeper profile ramp) or lengthens the correlation should come
    // here and restate the relation with its new numbers rather than silently pass.
    EXPECT_GT( r.PenetrationM, 0.0 );
    EXPECT_LT( r.PenetrationM / std::max( r.DecorrelationM, 1.0 ), 8.0 )
         << "the eye now looks through " << r.PenetrationM / r.DecorrelationM
         << " erosion correlation lengths; past about eight the up-rez cannot reach the silhouette at all "
            "and the layer is a smooth lump field whatever the Detail sliders say";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
