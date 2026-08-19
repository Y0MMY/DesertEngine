// What shape the clouds are, tested against the claims the shape model makes.
//
// Common/CloudField.glslh is compiled here AS C++ (CloudFieldReference.hpp), fed by the same noise
// functions the generator writes into the volume the march samples AND by the same profile table
// Graphic::CloudBuildProfileTable hands the device, so every number below is a number the GPU would
// produce. What is asserted, and each of it has already been got wrong once or is one edit from being:
//
//   * A SPECIES LIVES AT ITS OWN ALTITUDE, in kilometres, and the numbers are meteorology. A set of
//     ratios is satisfied at any absolute scale — a layer that drifted into the stratosphere would fail
//     no relation — so the anchor has to be a metre value or it is not an anchor.
//   * THREE SPECIES ARE THREE SHAPES. If stratus, congestus and cumulonimbus integrate to the same
//     profile they are three labels on one cloud, which is the exact failure this whole phase exists to
//     prevent.
//   * WHAT THE GENERATOR WRITES IS WHAT THE SHADER READS. The table is 16 384 texels nobody can inspect
//     on the device; a swapped axis or a half-texel offset is invisible in a frame and fatal to the
//     shape. This is the relation the double compilation is FOR.
//   * WHAT THE COVERAGE SLIDER MEANS, measured rather than asserted. A change to the layer geometry or
//     to the noise that silently moves the slider's meaning turns into a failing test instead of a sky
//     that "needs retuning".
//   * THE EROSION IS WEIGHTED BY DEPTH. Applied uniformly it removes the whole layer and leaves a
//     translucent veil, because a cut of fixed depth into a field whose typical value is small removes
//     the field. That is the difference between clouds and fog and it was got wrong once already.

#include "CloudFieldReference.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Desert::Tests::CloudFieldRef;

using Desert::Graphic::CloudBuildProfileTable;
using Desert::Graphic::CloudProfileCurve;
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

    // THE LAST TWO OF EACH ROW ARE T3'S: the placement scale and the stretch along the wind. Every fixture
    // here carries the identity pair, because what this suite tests is the PROFILE and a fixture that also
    // moved its patches would be changing two things between one measurement and the next. The one suite
    // that varies them is CloudFieldSpecies at the bottom of this file, which is about placement and
    // nothing else.

    // A SHEET: nearly the same height everywhere in a patch, thin, low.
    constexpr CloudTypeShape kSheet{ 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f,  0.0f,
                                     0.0f,  0.05f, 0.50f, 0.70f, 0.75f, 1.00f, 1.00f };
    // A HEAP: a fair-weather cumulus, half its height at the rim of a patch.
    constexpr CloudTypeShape kHeap{ 0.90f, 1.90f, 0.45f, 0.06f, 0.45f, 0.0f,  0.0f,
                                    0.0f,  0.70f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f };
    // A STORM: the only fixture with a second lobe, and the one the anvil tests are about.
    constexpr CloudTypeShape kStorm{ 0.90f, 9.00f, 0.12f, 0.04f, 0.40f, 9.5f,  1.8f,
                                     0.85f, 0.85f, 1.00f, 1.35f, 1.30f, 1.00f, 1.00f };

    // The ENVELOPE the march intersects for a shape, computed the way Graphic::PackCloudParams computes it
    // rather than restated. There is no authored layer thickness any more — that is the whole point of the
    // phase — so a constant here would be a second statement of the type's own altitudes.
    float EnvelopeThicknessKm( const CloudTypeShape& shape )
    {
        return CloudTypeTopKm( shape ) - CloudTypeBaseKm( shape );
    }

    // The component's defaults, converted to the kilometres this header works in exactly as
    // Graphic::PackCloudParams converts them. Written out rather than read from the component because
    // ComponentReflection owns the defaults; what this suite owns is what they PRODUCE.
    CloudFieldParams DefaultParams()
    {
        const CloudTypeShape& shape = DefaultShape();

        CloudFieldParams params;
        params.WeatherTileKm    = 12.0f; // Weather Tile Size 1 200 000 cm
        params.Coverage         = 0.10f; // Coverage
        params.CoverageContrast = 1.0f;  // Coverage Contrast
        params.DetailTileKm     = 4.0f;  // Detail Tile Size 400 000 cm
        params.DetailStrength   = 0.1f;  // Detail Strength
        // THE LAYER'S OWN SCALES, WITHOUT THE SPECIES' FACTORS IN THEM, which is where T3 moved the
        // product: with four kinds of cloud in one shell the multiply cannot be done once, so it happens
        // at the sample and the factor rides in CloudFieldSample.
        params.DensityScale = 1.0f;
        params.WindOffsetKm = vec3( 0.0f, 0.0f, 0.0f );

        // ONE SPECIES IN THE FIRST SLOT, table and arrays together, which is what a layer with a single
        // type in it is. Every test below that does not say otherwise is testing that layer.
        CloudBindSingleSpecies( params, shape );
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
        CloudFieldParams params = DefaultParams();
        params.Coverage         = coverage;

        const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );
        const float stepKm     = envelopeKm / static_cast<float>( heightSamples );

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

    // The highest ABSOLUTE altitude at which this shape has any body at all, at the fullest patch it can
    // stand in. Kilometres, because kilometres are the anchor.
    float ProfileTopKm( const CloudTypeShape& shape )
    {
        float top = 0.0f;
        for ( int step = 0; step <= 4000; ++step )
        {
            const float altitudeKm = 15.0f * static_cast<float>( step ) / 4000.0f;
            if ( CloudProfileCurve( shape, altitudeKm, 1.0f ) > 0.0f )
                top = altitudeKm;
        }
        return top;
    }

    // And the lowest.
    float ProfileBaseKm( const CloudTypeShape& shape )
    {
        for ( int step = 0; step <= 4000; ++step )
        {
            const float altitudeKm = 15.0f * static_cast<float>( step ) / 4000.0f;
            if ( CloudProfileCurve( shape, altitudeKm, 1.0f ) > 0.0f )
                return altitudeKm;
        }
        return 0.0f;
    }

    // How much cloud a shape is, in kilometre-units of profile, at a given fullness of patch. It is the
    // one number that says "these are different shapes" without depending on where either of them sits.
    float ProfileIntegralKm( const CloudTypeShape& shape, float pattern )
    {
        constexpr int kSteps = 4000;
        const float   stepKm = 15.0f / static_cast<float>( kSteps );

        float total = 0.0f;
        for ( int step = 0; step < kSteps; ++step )
        {
            const float altitudeKm = ( static_cast<float>( step ) + 0.5f ) * stepKm;
            total += CloudProfileCurve( shape, altitudeKm, pattern ) * stepKm;
        }
        return total;
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

TEST( CloudFieldProfile, ThereIsNothingOutsideAShapesOwnAltitudeBand )
{
    // The support of the curve is exactly [base, top] and there is no material a millimetre outside it.
    // This matters twice: the envelope the march intersects is computed FROM those two numbers, so a
    // curve that leaked past them would be cloud sliced off by the shell; and the table's rows are that
    // interval, so a leak would be silently clipped by the texture rather than seen.
    struct Fixture
    {
        const char*    Name;
        CloudTypeShape Shape;
    };

    for ( const Fixture& fixture : { Fixture{ "sheet", kSheet }, Fixture{ "heap", kHeap },
                                     Fixture{ "tower", DefaultShape() }, Fixture{ "storm", kStorm } } )
    {
        const CloudTypeShape& shape = fixture.Shape;

        const float baseKm = CloudTypeBaseKm( shape );
        const float topKm  = CloudTypeTopKm( shape );

        for ( const float pattern : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f } )
        {
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, baseKm, pattern ), 0.0f )
                 << fixture.Name << " has body at its own base";
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, topKm, pattern ), 0.0f )
                 << fixture.Name << " has body at its own ceiling";

            // And well outside, including the negative altitude a hand-written caller could ask for.
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, baseKm - 0.5f, pattern ), 0.0f );
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, topKm + 0.5f, pattern ), 0.0f );
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, -1.0f, pattern ), 0.0f );
        }

        // The measured support agrees with the two numbers the envelope is computed from. Written as a
        // separate check because the envelope is what PackCloudParams reads, and a curve that stopped
        // short of its declared top would waste shell rather than lose cloud — a different defect with
        // the same cause.
        EXPECT_GE( ProfileBaseKm( shape ), baseKm );
        EXPECT_LE( ProfileTopKm( shape ), topKm );
    }
}

TEST( CloudFieldProfile, ThreeShapesAreThreeShapesAndNotThreeLabels )
{
    // THE CRITERION THE WHOLE PROGRAMME EXISTS FOR. A library of types on top of one curve gives nine
    // captions to one cloud; the way to state "these are different clouds" without appealing to a
    // screenshot is that they hold different amounts of cloud in the vertical.
    //
    // Measured at a FULL patch, because that is where a type is most itself, and in kilometre-units so
    // that a shape which merely sits higher does not count as a different shape.
    const float sheet = ProfileIntegralKm( kSheet, 1.0f );
    const float tower = ProfileIntegralKm( DefaultShape(), 1.0f );
    const float storm = ProfileIntegralKm( kStorm, 1.0f );

    std::printf( "[CloudField] profile integrals at a full patch (km): sheet %.3f  tower %.3f  storm %.3f\n",
                 sheet, tower, storm );

    // Each is at least half again the one below it. A tolerance rather than "not equal": two shapes that
    // differ in the fourth decimal are two labels, and the show has to be visible.
    EXPECT_GT( tower, sheet * 1.5f ) << "a tower holds no more cloud than a sheet";
    EXPECT_GT( storm, tower * 1.5f ) << "a storm holds no more cloud than a tower";

    // And the fourth fixture is not a duplicate of one of the three either.
    const float heap = ProfileIntegralKm( kHeap, 1.0f );
    EXPECT_GT( heap, sheet * 1.5f );
    EXPECT_LT( heap, tower * 0.7f );
}

TEST( CloudFieldProfile, AFullerPatchIsATallerCloudOfTheSameType )
{
    // THE SECOND AXIS, and the reason the profile is a table at all. At the rim of a placement patch the
    // type is a flat pad; at its core it is the full thing. Stated as a relation over the axis rather
    // than at two points, because a table that had its axes swapped would still pass a two-point test at
    // some patterns.
    struct Fixture
    {
        const char*    Name;
        CloudTypeShape Shape;
    };

    for ( const Fixture& fixture :
          { Fixture{ "heap", kHeap }, Fixture{ "tower", DefaultShape() }, Fixture{ "storm", kStorm } } )
    {
        float previous = -1.0f;
        for ( int step = 0; step <= 20; ++step )
        {
            const float pattern  = 0.05f * static_cast<float>( step );
            const float integral = ProfileIntegralKm( fixture.Shape, pattern );

            EXPECT_GE( integral, previous - 1e-4f ) << fixture.Name << " holds LESS cloud at pattern " << pattern
                                                    << " than at the thinner patch below it";
            previous = integral;
        }

        // And the two ends genuinely differ, or the axis is decoration. The threshold is the one
        // ShapeModel.md §14 asks for by name: the integral at a thin patch is strictly below the one at a
        // full patch.
        EXPECT_LT( ProfileIntegralKm( fixture.Shape, 0.2f ), ProfileIntegralKm( fixture.Shape, 0.9f ) * 0.8f )
             << fixture.Name
             << " is the same height at the rim of a patch as in its middle, so the profile is still a "
                "curve wearing a table's clothes";
    }
}

TEST( CloudFieldProfile, TheCumulonimbusProfileHasTwoHumpsAndNoCurveCanDoThat )
{
    // THE ANVIL. It is the one claim in the phase that cannot be met by any parametric curve of the form
    // `baseRamp * topRamp`, which has exactly one maximum for any constants, and it is the reason D-13
    // says the runtime representation is a table.
    //
    // What is asserted is the SHAPE of the sequence and not the values: rising, falling to a genuine
    // trough, rising again. A single-humped profile cannot produce the middle term.
    const CloudTypeShape& shape = kStorm;

    // A patch full enough for the anvil to be present but not so full that the tower has grown into it.
    constexpr float kPattern = 0.55f;

    float towerPeak  = 0.0f;
    float trough     = 1.0f;
    float anvilPeak  = 0.0f;
    float towerTopKm = 0.0f;

    // The tower's band, the gap, and the anvil's band, split at altitudes the species' own numbers give.
    for ( int step = 0; step <= 2000; ++step )
    {
        const float altitudeKm = 12.0f * static_cast<float>( step ) / 2000.0f;
        const float value      = CloudProfileCurve( shape, altitudeKm, kPattern );

        if ( altitudeKm < 5.0f )
        {
            if ( value > towerPeak )
            {
                towerPeak  = value;
                towerTopKm = altitudeKm;
            }
        }
        else if ( altitudeKm < 7.5f )
        {
            trough = std::min( trough, value );
        }
        else
        {
            anvilPeak = std::max( anvilPeak, value );
        }
    }

    std::printf( "[CloudField] cumulonimbus at pattern %.2f: tower peak %.3f at %.2f km, trough %.3f, "
                 "anvil peak %.3f\n",
                 kPattern, towerPeak, towerTopKm, trough, anvilPeak );

    EXPECT_GT( towerPeak, 0.5f ) << "there is no tower";
    EXPECT_GT( anvilPeak, 0.1f ) << "there is no anvil";
    EXPECT_LT( trough, towerPeak * 0.2f ) << "the tower and the anvil are one hump, not two";
    EXPECT_LT( trough, anvilPeak * 0.5f ) << "the tower and the anvil are one hump, not two";
}

TEST( CloudFieldProfile, TheBaseIsASharpRampAndTheTopTapersOverNearlyHalfTheCloud )
{
    // The two ends are deliberately asymmetric because real cloud is: a cumulus base sits on the lifting
    // condensation level and is flat, while the top tapers over nearly half the cloud's height. A
    // symmetric profile — the obvious thing to write — gives every cloud a rounded bottom, which reads
    // as fog lying in the air rather than as a cloud sitting on a level.
    const CloudTypeShape& shape = DefaultShape();

    const float baseKm = shape.BaseAltitudeKm;
    const float topKm  = shape.TopAltitudeKm;
    const float spanKm = topKm - baseKm;

    // Full density is reached, or the coverage field would be multiplying something that never gets
    // there and no setting would produce a solid cloud.
    float peak = 0.0f;
    for ( int step = 0; step <= 2000; ++step )
        peak = std::max( peak, CloudProfileCurve( shape, baseKm + spanKm * step / 2000.0f, 1.0f ) );
    EXPECT_NEAR( peak, 1.0f, 1e-5f );

    // The base ramp is done inside the bottom twentieth of the cloud; the taper occupies half of it.
    EXPECT_NEAR( CloudProfileCurve( shape, baseKm + spanKm * 0.06f, 1.0f ), 1.0f, 1e-5f )
         << "the base ramp has not finished";
    EXPECT_LT( CloudProfileCurve( shape, baseKm + spanKm * 0.8f, 1.0f ), 0.9f ) << "the top is not tapering";
    EXPECT_GT( CloudProfileCurve( shape, baseKm + spanKm * 0.45f, 1.0f ), 0.95f ) << "the taper starts too low";
}

// ---------------------------------------------------------------------------------------------------
// The relation the double compilation is FOR: what the generator writes IS what the shader reads
// ---------------------------------------------------------------------------------------------------

TEST( CloudFieldProfileTable, TheShaderReadsBackExactlyWhatTheGeneratorWrote )
{
    // TWO SIDES THAT MUST AGREE, which is the defect class §2.3.1 of the contract is about, and here the
    // two sides are a C++ evaluator and a texture fetch written in GLSL. Nothing about a swapped axis, a
    // half-texel offset or a wrong normalisation is visible in a frame: a profile shifted twenty metres
    // still looks like a cloud. It is visible here to five decimal places.
    //
    // The shader side is CloudSampleProfileTable — the ACTUAL text the march compiles — reading the
    // ACTUAL buffer CloudBuildProfileTable produced, through a bilinear REPEAT sampler that is the one
    // VulkanImage2D creates.
    struct Fixture
    {
        const char*    Name;
        CloudTypeShape Shape;
    };

    for ( const Fixture& fixture : { Fixture{ "sheet", kSheet }, Fixture{ "heap", kHeap },
                                     Fixture{ "tower", DefaultShape() }, Fixture{ "storm", kStorm } } )
    {
        const CloudTypeShape& shape = fixture.Shape;
        CloudProfileTableSelect( shape );

        const float bottomKm = CloudTypeBaseKm( shape );
        const float spanKm   = CloudTypeTopKm( shape ) - bottomKm;

        float worst = 0.0f;

        // AT THE TEXEL CENTRES, where linear filtering is an identity, so the comparison is exact rather
        // than up to an interpolation error. Between them the table is a linear interpolation of a
        // function it samples at 47 metres, and asserting that would be asserting that the curve is
        // linear.
        for ( int j = 0; j < 64; ++j )
        {
            const float pattern = ( static_cast<float>( j ) + 0.5f ) / 64.0f;

            for ( int i = 0; i < 256; ++i )
            {
                const float fraction   = ( static_cast<float>( i ) + 0.5f ) / 256.0f;
                const float altitudeKm = bottomKm + fraction * spanKm;

                const float generated = CloudProfileCurve( shape, altitudeKm, pattern );
                const float read      = CloudSampleProfileTable( fraction, pattern, 0 );

                worst = std::max( worst, std::abs( generated - read ) );
            }
        }

        std::printf( "[CloudField] %s: worst generator-vs-shader disagreement %.7f\n", fixture.Name, worst );

        EXPECT_LT( worst, 1e-5f ) << fixture.Name
                                  << ": the table the shader reads is not the table the generator wrote";
    }

    CloudProfileTableSelect( DefaultShape() );
}

TEST( CloudFieldProfileTable, TheReadIsClampedSoTheLayerCeilingDoesNotWrapOntoItsFloor )
{
    // EVERY SAMPLER IN THIS ENGINE IS REPEAT (VulkanImage.cpp: addressModeU/V/W are hard-coded), and a
    // profile table is the one kind of image for which that is wrong. Without the clamp inside
    // CloudSampleProfileTable a height fraction of 1 reads the bottom row and the top of the layer grows
    // the cloud's own base back on top of itself — a lid, and one that no coverage setting removes.
    CloudProfileTableSelect( DefaultShape() );

    for ( const float pattern : { 0.0f, 0.5f, 1.0f } )
    {
        // THE CEILING IS EMPTY, always: the top of the envelope is the top of the tallest column this
        // species can grow, and the taper has run out by then. If the read wrapped, this would be reading
        // the FLOOR — which for a cumulus is a flat base at up to a third of full density — and the layer
        // would have a lid made of its own bottom.
        EXPECT_LT( CloudSampleProfileTable( 1.0f, pattern, 0 ), 0.02f ) << "the layer ceiling is not empty";

        // Out of range on the altitude axis reads as the nearest edge and NOT as the opposite one. This is
        // the wrap itself: a march that asked for 1.5 with a REPEAT sampler and no clamp would get the
        // value at 0.5.
        EXPECT_FLOAT_EQ( CloudSampleProfileTable( 1.5f, pattern, 0 ),
                         CloudSampleProfileTable( 1.0f, pattern, 0 ) );
        EXPECT_FLOAT_EQ( CloudSampleProfileTable( -0.5f, pattern, 0 ),
                         CloudSampleProfileTable( 0.0f, pattern, 0 ) );
    }

    // THE FLOOR IS DELIBERATELY NOT EMPTY, and it would be wrong if it were. The envelope's bottom IS the
    // species' condensation level, a cumulus base is genuinely abrupt, and the first row of the table
    // therefore carries real body — measured at about a third of full density for a thin column, whose
    // base ramp is a fraction of its own small height. A profile that faded in above the shell would be
    // fog, not cloud.
    EXPECT_GT( CloudSampleProfileTable( 0.0f, 0.0f, 0 ), 0.1f )
         << "the species' base has become a fade-in, so its clouds have rounded bottoms";

    // And the pattern axis does not wrap either: a pattern past 1 must read as a full patch and not as an
    // empty one.
    EXPECT_NEAR( CloudSampleProfileTable( 0.5f, 1.5f, 0 ), CloudSampleProfileTable( 0.5f, 1.0f, 0 ), 1e-5f );
    EXPECT_NEAR( CloudSampleProfileTable( 0.5f, -0.5f, 0 ), CloudSampleProfileTable( 0.5f, 0.0f, 0 ), 1e-5f );
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
    // MEASURED HERE, and re-measured whenever the field underneath it changes
    // (32 columns, 36 heights, contrast 1, layer and volume as shipped) — the row this test prints:
    //
    //     Coverage    0.05   0.10   0.15   0.20   0.30   0.50
    //     opaque       17%    36%    49%    60%    75%    95%
    //
    // THE ROW HAS MOVED THREE TIMES AND EACH MOVE HAS A NAMED CAUSE. While the coverage field was two
    // octaves of Perlin-Worley it read 0 / 1 / 4(at 0.12) / 20 / 58 / 98; when the volume became an asset
    // and the field became two frequencies of Curly-Alligator it read 0 / 0 / 0 / 4 / 50 / 100; the
    // quantile mapping then made the row a property of the LAYER GEOMETRY rather than of the noise, and
    // it read 11 / 25 / 40 / 52 / 71 / 95.
    //
    // THIS MOVE IS THE GEOMETRY, exactly as that arrangement promised. The envelope used to be an
    // authored ten kilometres of which a cloud filled a fraction; it is now the SPECIES' OWN [base, top],
    // and the shipped species is a congestus that fills all 3.6 km of it. A column that carries cloud
    // therefore carries three times as much of it, and the same threshold hides the sky behind more of
    // them. The default moved from 0.15 to 0.10 with it, to the point that reproduces the ~40 % sky cover
    // the previous default produced — the slider means something different, the sky looks the same.
    //
    // The tolerances are wide enough to survive a reseed and narrow enough to catch a shift the size of
    // any of the three above.
    constexpr int kColumns = 32;
    constexpr int kHeights = 36;

    const SkyCover atFive    = MeasureSkyCover( 0.05f, kColumns, kHeights );
    const SkyCover atTen     = MeasureSkyCover( 0.10f, kColumns, kHeights ); // the shipped default
    const SkyCover atFifteen = MeasureSkyCover( 0.15f, kColumns, kHeights );
    const SkyCover atTwenty  = MeasureSkyCover( 0.20f, kColumns, kHeights );
    const SkyCover atThirty  = MeasureSkyCover( 0.30f, kColumns, kHeights );
    const SkyCover atFifty   = MeasureSkyCover( 0.50f, kColumns, kHeights );

    std::printf( "[CloudField] opaque sky cover: 0.05 %.0f%%  0.10 %.0f%%  0.15 %.0f%%  0.20 %.0f%%  "
                 "0.30 %.0f%%  0.50 %.0f%%\n",
                 atFive.Opaque * 100.0f, atTen.Opaque * 100.0f, atFifteen.Opaque * 100.0f,
                 atTwenty.Opaque * 100.0f, atThirty.Opaque * 100.0f, atFifty.Opaque * 100.0f );
    std::printf( "[CloudField] at the default the sky is %.0f%% TOUCHED by cloud and %.0f%% hidden by it\n",
                 atTen.Touched * 100.0f, atTen.Opaque * 100.0f );

    // The pinned table, with a tolerance of ten points of sky — a reseed moves individual clouds and not
    // the statistic, so anything larger than this is the mapping itself having moved.
    EXPECT_NEAR( atFive.Opaque, 0.17f, 0.10f );
    EXPECT_NEAR( atTen.Opaque, 0.36f, 0.10f );
    EXPECT_NEAR( atFifteen.Opaque, 0.49f, 0.10f );
    EXPECT_NEAR( atTwenty.Opaque, 0.60f, 0.12f );
    EXPECT_NEAR( atThirty.Opaque, 0.75f, 0.12f );
    EXPECT_GT( atFifty.Opaque, 0.90f ) << "the top half of the slider is not saturated";

    // THE DEFAULT IS A SCATTERED SKY AND NOT AN OVERCAST, which is what its tooltip promises and what the
    // frame has to show. Two-fifths hidden is the meteorological "scattered to broken"; past two-thirds
    // the sky reads as a lid whatever the noise is doing.
    EXPECT_GT( atTen.Opaque, 0.25f ) << "the default leaves the sky nearly empty";
    EXPECT_LT( atTen.Opaque, 0.60f ) << "the default is an overcast";

    // Strictly rising ACROSS the band, which is the part an artist feels. Ten points of slider must buy
    // at least ten points of sky where the band actually is, or the control is mush exactly where it is
    // meant to work.
    EXPECT_GT( atTen.Opaque, atFive.Opaque + 0.15f );
    EXPECT_GT( atThirty.Opaque, atTen.Opaque + 0.15f );

    // And the thin half of the answer, which is the one that has bitten this programme before: a sky
    // that is mostly TOUCHED by cloud and hardly at all hidden by it is a sky full of veil.
    EXPECT_GT( atTen.Touched, atTen.Opaque );
}

TEST( CloudFieldCoverage, TheFieldTheSliderThresholdsIsItsOwnQuantileSoTheSliderSurvivesANewVolume )
{
    // THE PROPERTY THE ASSET SLOT NEEDS, and the one the two shifts recorded above would each have failed.
    //
    // `Coverage` is a threshold on the coverage field, so what a setting MEANS is decided by that field's
    // distribution. Twice now the distribution has changed under it — Perlin-Worley to Curly-Alligator,
    // and the wispy pair to the billowy one — and each time the whole slider moved without anything
    // failing. Now that the volume is an artist's asset the same shift is one drag-and-drop away.
    //
    // Common/CloudField.glslh removes the dependency by mapping the blended field through its own
    // cumulative distribution: the value handed to the threshold is the FRACTION of the field below it,
    // so the field is uniform on [0, 1] by construction and the threshold's linear sweep of [0, 1] is
    // finally reading what it was written for. Uniform means quantile equals value, and that is what is
    // asserted — one statement that pins the calibration constant, the channel choice and the weights at
    // once, and that a differently baked volume must also satisfy.
    CloudFieldParams params = DefaultParams();

    std::vector<float> field;
    field.reserve( 40 * 40 * 40 );

    // One period of the coverage field in every direction, so this is the population and not a sample of
    // one neighbourhood. The height range is the layer's, because the field is sampled in full 3D.
    for ( int iz = 0; iz < 40; ++iz )
        for ( int iy = 0; iy < 40; ++iy )
            for ( int ix = 0; ix < 40; ++ix )
            {
                const vec3 uvw( ( ix + 0.5f ) / 40.0f, ( iy + 0.5f ) / 40.0f, ( iz + 0.5f ) / 40.0f );
                const vec4 noise = CLOUD_SAMPLE_NOISE( uvw );

                // The same two channels and the same two weights Common/CloudField.glslh blends, followed
                // by the same calibration. Stated here rather than reached through SampleCloudField
                // because what is under test is the DISTRIBUTION of that intermediate, and the vertical
                // profile downstream of it would mask exactly the property being asserted.
                const float blended = noise.z * 0.65f + noise.w * 0.35f;
                field.push_back( glm::smoothstep( 0.5f - 0.32f, 0.5f + 0.32f, blended ) );
            }

    std::sort( field.begin(), field.end() );

    const auto quantile = [&]( float f ) { return field[static_cast<size_t>( f * ( field.size() - 1 ) )]; };

    std::printf( "[CloudField] coverage field quantiles: p05 %.3f p25 %.3f p50 %.3f p75 %.3f p95 %.3f\n",
                 quantile( 0.05f ), quantile( 0.25f ), quantile( 0.50f ), quantile( 0.75f ), quantile( 0.95f ) );

    // Uniform to within five points of quantile. Tighter than that would be asserting that a smoothstep
    // IS a normal cumulative distribution, which it only approximates; looser would not catch the two
    // shifts this exists to prevent, both of which moved the median by more than a tenth.
    for ( const float f : { 0.05f, 0.25f, 0.50f, 0.75f, 0.95f } )
        EXPECT_NEAR( quantile( f ), f, 0.05f )
             << "the coverage field's " << f * 100.0f << "th percentile is not at " << f
             << ", so a Coverage setting no longer selects the fraction of the field it names";
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
                const float heightFraction = ( static_cast<float>( ih ) + 0.5f ) / kHeights;
                const vec3  positionKm( x, heightFraction * EnvelopeThicknessKm( DefaultShape() ), z );

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

            const float low  = SampleCloudField( params, kHeightFraction, vec3( x, 0.2f, z ) ).Profile;
            const float high = SampleCloudField( params, kHeightFraction, vec3( x, 3.4f, z ) ).Profile;

            if ( std::abs( low - high ) > 1e-4f )
                ++disagreements;
        }
    }

    EXPECT_GT( disagreements, 24 * 24 / 4 )
         << "the field gives the same answer at every altitude in a column, so it is a plane extruded "
            "vertically and every cloud in the sky is a curtain";
}

TEST( CloudFieldProducer, TheThickPartOfAPatchIsWhereTheTowerIsAndTheRimIsFlat )
{
    // THE PROPERTY THE WHOLE TABLE WAS BUILT FOR, asserted where it actually matters — in the sky, not on
    // the curve. It replaces the test of Cloud Type Variance, which asserted that the layer had no lid;
    // that test is answered here in a stronger form, because a spread of heights is easy to produce with
    // a noise and USELESS if it is uncorrelated with where the cloud is. A tower on a wisp is the defect
    // the variance had and the table does not.
    //
    // Measured per column: how much cloud the column holds, and how high the cloud in it reaches. The
    // claim is that the two go together.
    CloudFieldParams params = DefaultParams();
    params.Coverage         = 0.35f; // enough columns with cloud in them for the quartiles to mean something

    constexpr int kColumns = 40;
    constexpr int kHeights = 48;

    const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    struct Column
    {
        float Mass;
        float TopFraction;
    };

    std::vector<Column> columns;

    for ( int iz = 0; iz < kColumns; ++iz )
    {
        for ( int ix = 0; ix < kColumns; ++ix )
        {
            const float x = params.WeatherTileKm * ( static_cast<float>( ix ) + 0.5f ) / kColumns;
            const float z = params.WeatherTileKm * ( static_cast<float>( iz ) + 0.5f ) / kColumns;

            float mass = 0.0f;
            float top  = 0.0f;

            for ( int ih = 0; ih < kHeights; ++ih )
            {
                const float heightFraction = ( static_cast<float>( ih ) + 0.5f ) / kHeights;
                const vec3  positionKm( x, heightFraction * envelopeKm, z );

                const float profile = SampleCloudField( params, heightFraction, positionKm ).Profile;
                if ( profile <= 0.0f )
                    continue;

                mass += profile;
                top = heightFraction;
            }

            if ( mass > 0.0f )
                columns.push_back( Column{ mass, top } );
        }
    }

    ASSERT_GT( columns.size(), 200u ) << "too few columns carry cloud for the comparison to mean anything";

    std::sort( columns.begin(), columns.end(),
               []( const Column& a, const Column& b ) { return a.Mass < b.Mass; } );

    const size_t quartile = columns.size() / 4;

    float thinTop  = 0.0f;
    float thickTop = 0.0f;
    for ( size_t i = 0; i < quartile; ++i )
    {
        thinTop += columns[i].TopFraction;
        thickTop += columns[columns.size() - 1 - i].TopFraction;
    }
    thinTop /= static_cast<float>( quartile );
    thickTop /= static_cast<float>( quartile );

    std::printf( "[CloudField] mean cloud top: thinnest quarter of columns %.0f%% of the envelope, "
                 "thickest quarter %.0f%%\n",
                 thinTop * 100.0f, thickTop * 100.0f );

    // A FIFTH OF THE ENVELOPE between the rim of a patch and its core, and the number is measured rather
    // than chosen: the shipped field gives 40 % against 67 %, a spread of 0.27, on a 3.6 km envelope —
    // nine hundred metres of difference in cloud top between the thin columns and the thick ones. The
    // floor is set below the measurement by enough to survive a reseed and above zero by enough that a
    // profile which stopped depending on the pattern would fail here rather than in a frame.
    EXPECT_GT( thickTop - thinTop, 0.20f )
         << "the thick middle of a patch reaches no higher than its rim, so the profile table's second "
            "axis is doing nothing";
}

TEST( CloudFieldProducer, TheEdgeCharacterIsTheSpeciesAndDoesNotWanderAcrossTheSky )
{
    // The species decides whether its erosion is wispy or billowy, and where there is only one species it
    // decides it for the whole layer. The scalar it replaced was mixed with a noise per sample, which meant
    // the CHARACTER of an edge changed within one cloud — a stratus rim on one side of a cumulus and a
    // cumulus rim on the other.
    //
    // WHERE THERE IS NO CLOUD THERE IS NO CHARACTER, and that changed with T3. A sample the union left
    // empty carries zeroes, because the edge numbers are now the WINNING species' and an empty sample has
    // no winner. Nothing reads them — CloudSampleDensity returns at a zero profile before it touches
    // DetailType — and filling them from a species that did not win would be exactly the averaging across
    // species this phase refuses. So the assertion is made where it means something.
    CloudFieldParams params = DefaultParams();

    const float envelopeKm = EnvelopeThicknessKm( DefaultShape() );

    int inCloud = 0;

    for ( int iz = 0; iz < 24; ++iz )
    {
        for ( int ix = 0; ix < 24; ++ix )
        {
            const vec3 position( 8.0f * ( ix + 0.5f ) / 24.0f, 0.4f * envelopeKm, 8.0f * ( iz + 0.5f ) / 24.0f );

            const CloudFieldSample sample = SampleCloudField( params, 0.4f, position );
            if ( sample.Profile <= 0.0f )
                continue;

            ++inCloud;
            EXPECT_FLOAT_EQ( sample.DetailType, params.SpeciesEdge[0].x )
                 << "the erosion character wandered away from the species";
        }
    }

    EXPECT_GT( inCloud, 0 ) << "no sample in the grid was inside cloud, so the loop above asserted nothing";
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

        CloudFieldParams params = DefaultParams();
        params.Coverage         = coverage;

        CloudProfileTableSelectSet( pair, 2u );
        CloudBindSpecies( params, pair, 2u, vec3( 1.0f, 0.0f, 0.0f ) );
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
    // ONE SPECIES' PROFILE AT ONE POINT, built the way the producer builds it.
    //
    // A DELIBERATE MIRROR OF THE COORDINATE, and the test below is what keeps it honest: it compares the
    // union the producer returns against the max of what this computes, so a mirror that drifted from the
    // producer fails the test rather than making it vacuous. There is no other way to ask "what would
    // species k alone have said here" — the slot decides both the table's channel and the field's offset,
    // so a species cannot be moved into slot 0 to be measured on its own.
    float SpeciesProfileAt( const CloudFieldParams& params, int slot, float fraction, vec3 positionKm )
    {
        const vec4 basis      = params.SpeciesPlacement[slot];
        const vec2 along      = vec2( basis.x, basis.y );
        const vec2 across     = vec2( basis.z, basis.w );
        const vec2 horizontal = vec2( positionKm.x, positionKm.z );

        const float verticalFreq = CLOUD_COVERAGE_FREQ_Y * length( across ) / CLOUD_COVERAGE_FREQ_Z;

        const vec3 texturePos = vec3( dot( horizontal, along ) / params.WeatherTileKm,
                                      positionKm.y * verticalFreq / params.WeatherTileKm,
                                      dot( horizontal, across ) / params.WeatherTileKm ) +
                                CloudSpeciesPlacementOffset( slot );

        return CloudSpeciesProfile( params, CLOUD_SAMPLE_NOISE( texturePos ), fraction, slot );
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

    const vec4 alive = CloudSampleProfileTableAll( fraction, 1.0f );
    ASSERT_GT( alive.x, 0.0f ) << "the deck's channel is empty inside its own band";
    ASSERT_GT( alive.y, 0.0f ) << "the tower's channel is empty inside its own band";

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
            const vec3 position( 24.0f * ( ix + 0.5f ) / kColumns, altitudeKm - kDeck.BaseAltitudeKm,
                                 24.0f * ( iz + 0.5f ) / kColumns );

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
            const float expected = deck > tower ? kDeck.DetailCharacter : kTower.DetailCharacter;
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

TEST( CloudFieldSpecies, EverySpeciesSeesTheSameDistributionSoOneCoverageSliderMeansOneThing )
{
    // THE NUMBER THAT DECIDES WHERE A SPECIES' PLACEMENT FIELD COMES FROM. Each species reads the same
    // channel pair of the same volume at its own scale, its own stretch and its own offset — and the
    // argument for that, rather than for "a channel of the volume each", is statistical.
    //
    // The quantile map in the producer is calibrated to ONE distribution (the Alligator blend's, half
    // width 0.32). `Coverage` selects a FRACTION of the field, so a species reading a differently
    // distributed field would answer the same setting with a different fraction of sky — which is exactly
    // the defect the quantile map was introduced to remove, put back once per species. A change of scale,
    // of stretch and of offset leaves the distribution alone, because the field is statistically
    // stationary; a change of channel does not, because the wispy pair is `1 - Alligator`.
    //
    // WHAT IS ASSERTED IS AGREEMENT, NOT A VALUE. The absolute quantiles of this particular grid are not
    // the calibration data — Desert/Tests/Engine/CloudField's own quantile test above owns those, over the
    // population the producer actually reads. What matters here is that the four species agree with each
    // other, because that is precisely what makes one Coverage slider mean one thing for all of them.
    const CloudTypeShape four[4] = {
         kDeck,  // scale 0.35, stretched 1.6 along the wind
         kTower, // scale 1.00, round
         CloudTypeShape{ 0.60f, 2.60f, 0.80f, 0.10f, 0.35f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 2.50f,
                         8.00f }, // a cirrus' placement
         CloudTypeShape{ 0.60f, 2.60f, 0.80f, 0.10f, 0.35f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.80f,
                         0.20f }, // a lenticular's, stretched ACROSS the wind
    };

    CloudFieldParams params = DefaultParams();
    CloudBindSpecies( params, four, 4u, vec3( 1.0f, 0.0f, 0.0f ) );

    constexpr int kColumns   = 96;
    constexpr int kQuantiles = 3;

    // p25, p50, p75 — three points of the curve rather than one, because two distributions can share a
    // median and differ everywhere else.
    const float fractions[kQuantiles] = { 0.25f, 0.50f, 0.75f };

    float measured[4][kQuantiles] = {};

    for ( int slot = 0; slot < 4; ++slot )
    {
        std::vector<float> field;
        field.reserve( kColumns * kColumns );

        for ( int iz = 0; iz < kColumns; ++iz )
        {
            for ( int ix = 0; ix < kColumns; ++ix )
            {
                const vec3 position( 60.0f * ( ix + 0.5f ) / kColumns, 1.7f, 60.0f * ( iz + 0.5f ) / kColumns );

                const vec4  basis        = params.SpeciesPlacement[slot];
                const vec2  along        = vec2( basis.x, basis.y );
                const vec2  across       = vec2( basis.z, basis.w );
                const vec2  horizontal   = vec2( position.x, position.z );
                const float verticalFreq = CLOUD_COVERAGE_FREQ_Y * length( across ) / CLOUD_COVERAGE_FREQ_Z;

                const vec3 texturePos = vec3( dot( horizontal, along ) / params.WeatherTileKm,
                                              position.y * verticalFreq / params.WeatherTileKm,
                                              dot( horizontal, across ) / params.WeatherTileKm ) +
                                        CloudSpeciesPlacementOffset( slot );

                const vec4  noise   = CLOUD_SAMPLE_NOISE( texturePos );
                const float blended = noise.z * 0.65f + noise.w * 0.35f;
                field.push_back( smoothstep( 0.5f - 0.32f, 0.5f + 0.32f, blended ) );
            }
        }

        std::sort( field.begin(), field.end() );
        for ( int q = 0; q < kQuantiles; ++q )
            measured[slot][q] = field[static_cast<size_t>( fractions[q] * ( field.size() - 1 ) )];
    }

    for ( int slot = 0; slot < 4; ++slot )
    {
        std::printf( "[CloudFieldSpecies] species %d (scale %.2f, stretch %.2f): p25 %.3f  p50 %.3f  p75 %.3f\n",
                     slot, four[slot].PlacementScale, four[slot].PlacementAnisotropy, measured[slot][0],
                     measured[slot][1], measured[slot][2] );
    }

    for ( int q = 0; q < kQuantiles; ++q )
    {
        float low  = measured[0][q];
        float high = measured[0][q];
        for ( int slot = 1; slot < 4; ++slot )
        {
            low  = std::min( low, measured[slot][q] );
            high = std::max( high, measured[slot][q] );
        }

        EXPECT_LT( high - low, 0.06f )
             << "the four species disagree by " << ( high - low ) << " at the " << fractions[q]
             << " quantile, so one Coverage setting selects a different fraction of sky for each of them";
    }

    // AND THE SPREAD IS THE SAMPLING ERROR, not zero: four different scales read four different parts of
    // the same field, so identical quantiles would mean the offsets are not decorrelating anything.
    float widest = 0.0f;
    for ( int q = 0; q < kQuantiles; ++q )
    {
        float low  = measured[0][q];
        float high = measured[0][q];
        for ( int slot = 1; slot < 4; ++slot )
        {
            low  = std::min( low, measured[slot][q] );
            high = std::max( high, measured[slot][q] );
        }
        widest = std::max( widest, high - low );
    }
    EXPECT_GT( widest, 0.0f ) << "the four species read the identical field, so the per-slot offsets are "
                                 "not decorrelating them at all";
}

TEST( CloudFieldSpecies, TheAnisotropyStretchesThePatchesAlongTheWindAndNotAcrossIt )
{
    // DEFECT ONE OF THE THREE T3 WAS HANDED: cirrus reads as a mackerel sky rather than as fibrous bands,
    // and no profile can fix it because a profile does not decide the shape of a patch in plan.
    //
    // Measured as the DECORRELATION LENGTH of the placement field along each of the two axes: how far you
    // have to walk before the field stops resembling itself. A round patch gives the same answer both
    // ways; a band gives a longer one downwind, and the ratio of the two is the thing the control claims
    // to set.
    const CloudTypeShape round{ 2.20f, 5.80f, 0.15f, 0.04f, 0.50f, 0.0f,  0.0f,
                                0.0f,  1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f };
    CloudTypeShape       streaked = round;
    streaked.PlacementAnisotropy  = 8.0f;

    const auto correlationAt = []( const CloudTypeShape& shape, float alongKm, float acrossKm )
    {
        const Desert::Graphic::CloudPlacementBasis basis =
             Desert::Graphic::CloudSpeciesPlacementBasis( shape, 1.0f, 0.0f );

        constexpr int   kSamples = 40;
        constexpr float kTileKm  = 12.0f;

        double sum = 0.0;
        for ( int iz = 0; iz < kSamples; ++iz )
        {
            for ( int ix = 0; ix < kSamples; ++ix )
            {
                const vec2 origin( 60.0f * ( ix + 0.5f ) / kSamples, 60.0f * ( iz + 0.5f ) / kSamples );
                const vec2 moved( origin.x + alongKm, origin.y + acrossKm );

                const auto sample = [&]( vec2 horizontal )
                {
                    const vec2 along( basis.AlongX, basis.AlongZ );
                    const vec2 across( basis.AcrossX, basis.AcrossZ );
                    const vec3 texturePos( dot( horizontal, along ) / kTileKm, 0.0f,
                                           dot( horizontal, across ) / kTileKm );
                    const vec4 noise = CLOUD_SAMPLE_NOISE( texturePos );
                    return noise.z * 0.65f + noise.w * 0.35f;
                };

                sum += std::abs( sample( origin ) - sample( moved ) );
            }
        }
        return static_cast<float>( sum / ( kSamples * kSamples ) );
    };

    // One kilometre of walk, once downwind and once across it.
    const float roundAlong   = correlationAt( round, 1.0f, 0.0f );
    const float roundAcross  = correlationAt( round, 0.0f, 1.0f );
    const float streakAlong  = correlationAt( streaked, 1.0f, 0.0f );
    const float streakAcross = correlationAt( streaked, 0.0f, 1.0f );

    std::printf( "[CloudFieldSpecies] mean |difference| over 1 km — round %.4f along / %.4f across, "
                 "streaked %.4f along / %.4f across\n",
                 roundAlong, roundAcross, streakAlong, streakAcross );

    // A ROUND PATCH CHANGES BY ABOUT AS MUCH EITHER WAY. Not exactly: the two horizontal frequencies
    // differ by the 6.65 % Unreal's own coefficients carry, which is why this is a loose bound and not an
    // equality.
    EXPECT_NEAR( roundAlong / roundAcross, 1.0f, 0.25f ) << "an anisotropy of 1 is not producing a round patch";

    // A STREAKED ONE CHANGES FAR LESS DOWNWIND, because that is what a band is.
    EXPECT_LT( streakAlong, 0.5f * streakAcross )
         << "an anisotropy of 8 did not draw the patches out along the wind";

    // AND THE ACROSS-WIND SCALE IS UNTOUCHED, which is the property that makes the control one control
    // rather than two: stretching a cirrus must not also make it coarser side to side.
    EXPECT_NEAR( streakAcross, roundAcross, 0.02f )
         << "the stretch along the wind moved the period across it as well";
}

TEST( CloudFieldSpecies, TheFrequencyTRIPLESAreTheSameOnBothSidesOfTheSeam )
{
    // TWO STATEMENTS OF THREE NUMBERS — the #defines in Common/CloudField.glslh and the constants in
    // Engine/Graphic/Clouds/CloudProfileTable.hpp that the packer builds the basis vectors from. This is
    // the only place both spellings are in scope, so this is where they are held equal. Without it a
    // change to one produces a placement field whose frequency the shader and the CPU disagree about, and
    // the symptom is a sky that is subtly the wrong scale with nothing in any log.
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudCoverageFreqX, CLOUD_COVERAGE_FREQ_X );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudCoverageFreqY, CLOUD_COVERAGE_FREQ_Y );
    EXPECT_FLOAT_EQ( Desert::Graphic::kCloudCoverageFreqZ, CLOUD_COVERAGE_FREQ_Z );

    // And the ceiling on how many kinds of cloud a sky holds, which is the width of a texel on one side
    // and a #define on the other.
    EXPECT_EQ( static_cast<int>( Desert::Graphic::kCloudSpeciesSlots ), CLOUD_SPECIES_SLOTS );
}

TEST( CloudFieldSpecies, AnEmptySetStillHasASkyAndAnUnfilledSlotCostsNothing )
{
    // THE EMPTY SET IS A DOCUMENTED ANSWER, and the renderer's answer to it is one built-in congestus —
    // which is a set of one and therefore already covered above. What is tested HERE is the other half of
    // that promise: that slots past the count cannot put cloud in the sky even if their arrays were
    // filled, because the profile table's channel for them is zero everywhere.
    const CloudTypeShape one[1] = { kTower };

    CloudFieldParams params = DefaultParams();
    params.Coverage         = 0.9f; // a nearly solid sky, so an unfilled slot leaking would be obvious

    CloudProfileTableSelectSet( one, 1u );
    // Deliberately hostile: the arrays carry a SECOND species with a large density, and only the count
    // and the table's empty channel stand between it and the frame.
    const CloudTypeShape two[2] = { kTower, kDeck };
    CloudBindSpecies( params, two, 2u, vec3( 1.0f, 0.0f, 0.0f ) );
    params.SpeciesCount = 1; // ...but the layer says there is one

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

    // And the table itself: channel 1 of a one-species table is zero at every altitude and every pattern,
    // which is the second, independent reason the slot cannot be read.
    for ( int i = 0; i <= 32; ++i )
    {
        const float fraction = static_cast<float>( i ) / 32.0f;
        const vec4  profiles = CloudSampleProfileTableAll( fraction, 1.0f );

        EXPECT_FLOAT_EQ( profiles.y, 0.0f ) << "an unwritten channel is not zero";
        EXPECT_FLOAT_EQ( profiles.z, 0.0f ) << "an unwritten channel is not zero";
        EXPECT_FLOAT_EQ( profiles.w, 0.0f ) << "an unwritten channel is not zero";
    }

    CloudProfileTableSelect( DefaultShape() );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
