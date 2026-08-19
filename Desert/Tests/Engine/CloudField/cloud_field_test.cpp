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
using Desert::Graphic::CloudSpecies;
using Desert::Graphic::CloudSpeciesBaseKm;
using Desert::Graphic::CloudSpeciesShape;
using Desert::Graphic::CloudSpeciesShapeOf;
using Desert::Graphic::CloudSpeciesTopKm;

namespace
{
    // The species the coverage measurements below are made on: the component's own default, and the one
    // whose family of profiles varies most across a patch.
    constexpr CloudSpecies kDefaultSpecies = CloudSpecies::CumulusCongestus;

    // The ENVELOPE the march intersects for that species, computed the way Graphic::PackCloudParams
    // computes it rather than restated. There is no authored layer thickness any more — that is the whole
    // point of the phase — so a constant here would be a second statement of the species' own altitudes.
    float EnvelopeThicknessKm( CloudSpecies species )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );
        return CloudSpeciesTopKm( shape ) - CloudSpeciesBaseKm( shape );
    }

    // The component's defaults, converted to the kilometres this header works in exactly as
    // Graphic::PackCloudParams converts them. Written out rather than read from the component because
    // ComponentReflection owns the defaults; what this suite owns is what they PRODUCE.
    CloudFieldParams DefaultParams()
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( kDefaultSpecies );

        CloudFieldParams params;
        params.WeatherTileKm    = 12.0f; // Weather Tile Size 1 200 000 cm
        params.Coverage         = 0.10f; // Coverage
        params.CoverageContrast = 1.0f;  // Coverage Contrast
        params.DetailCharacter  = shape.DetailCharacter;
        params.DetailTileKm     = 4.0f; // Detail Tile Size 400 000 cm
        params.DetailStrength   = 0.1f; // Detail Strength
        // The packer folds the species' own density into the artist's scale, so the product is what the
        // shader sees and the product is what this drives.
        params.DensityScale = 1.0f * shape.DensityFactor;
        params.WindOffsetKm = vec3( 0.0f, 0.0f, 0.0f );
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

        const float envelopeKm = EnvelopeThicknessKm( kDefaultSpecies );
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

    // The highest ABSOLUTE altitude at which this species has any body at all, at the fullest patch it
    // can stand in. Kilometres, because kilometres are the anchor.
    float ProfileTopKm( CloudSpecies species )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

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
    float ProfileBaseKm( CloudSpecies species )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

        for ( int step = 0; step <= 4000; ++step )
        {
            const float altitudeKm = 15.0f * static_cast<float>( step ) / 4000.0f;
            if ( CloudProfileCurve( shape, altitudeKm, 1.0f ) > 0.0f )
                return altitudeKm;
        }
        return 0.0f;
    }

    // How much cloud a species is, in kilometre-units of profile, at a given fullness of patch. It is the
    // one number that says "these are different shapes" without depending on where either of them sits.
    float ProfileIntegralKm( CloudSpecies species, float pattern )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

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
// The profile generator — where a species lives, and whether three of them are three shapes
// ---------------------------------------------------------------------------------------------------

TEST( CloudFieldProfile, ThereIsNothingOutsideTheSpeciesOwnAltitudeBand )
{
    // The support of the curve is exactly [base, top] and there is no material a millimetre outside it.
    // This matters twice: the envelope the march intersects is computed FROM those two numbers, so a
    // curve that leaked past them would be cloud sliced off by the shell; and the table's rows are that
    // interval, so a leak would be silently clipped by the texture rather than seen.
    for ( const CloudSpecies species : { CloudSpecies::Stratus, CloudSpecies::CumulusMediocris,
                                         CloudSpecies::CumulusCongestus, CloudSpecies::Cumulonimbus } )
    {
        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

        const float baseKm = CloudSpeciesBaseKm( shape );
        const float topKm  = CloudSpeciesTopKm( shape );

        for ( const float pattern : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f } )
        {
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, baseKm, pattern ), 0.0f )
                 << "species " << static_cast<int>( species ) << " has body at its own base";
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, topKm, pattern ), 0.0f )
                 << "species " << static_cast<int>( species ) << " has body at its own ceiling";

            // And well outside, including the negative altitude a hand-written caller could ask for.
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, baseKm - 0.5f, pattern ), 0.0f );
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, topKm + 0.5f, pattern ), 0.0f );
            EXPECT_FLOAT_EQ( CloudProfileCurve( shape, -1.0f, pattern ), 0.0f );
        }

        // The measured support agrees with the two numbers the envelope is computed from. Written as a
        // separate check because the envelope is what PackCloudParams reads, and a curve that stopped
        // short of its declared top would waste shell rather than lose cloud — a different defect with
        // the same cause.
        EXPECT_GE( ProfileBaseKm( species ), baseKm );
        EXPECT_LE( ProfileTopKm( species ), topKm );
    }
}

TEST( CloudFieldProfile, EverySpeciesSitsWhereMeteorologyPutsIt )
{
    // THE ABSOLUTE ANCHOR (Docs/Clouds/ANALYSIS_APPROACH.md §5.1). Every relation in this subsystem —
    // tile against thickness, step against thickness, view distance against tile — is a RATIO, and a set
    // of ratios is satisfied at any absolute scale whatsoever. Move the whole layer to twelve kilometres
    // and not one relation notices. These are the numbers that notice, and they are metres rather than
    // fractions for exactly that reason.
    //
    // Stratus: a sheet that lies on the ground and does not reach 600 m.
    EXPECT_LE( ProfileTopKm( CloudSpecies::Stratus ), 0.6f )
         << "stratus has grown into something that is not stratus";
    EXPECT_GE( ProfileBaseKm( CloudSpecies::Stratus ), 0.0f );

    // Cumulus mediocris: the whole cloud lives between 0.8 and 2.0 km.
    EXPECT_GE( ProfileBaseKm( CloudSpecies::CumulusMediocris ), 0.8f );
    EXPECT_LE( ProfileTopKm( CloudSpecies::CumulusMediocris ), 2.0f );

    // Cumulonimbus: it BEGINS between 0.5 and 1.5 km. Where it ends is the tropopause and is not bounded
    // by the same rule — that is the difference between a base and an extent, and getting it the other
    // way round would forbid the species from being what it is.
    EXPECT_GE( ProfileBaseKm( CloudSpecies::Cumulonimbus ), 0.5f );
    EXPECT_LE( ProfileBaseKm( CloudSpecies::Cumulonimbus ), 1.5f );
    EXPECT_GT( ProfileTopKm( CloudSpecies::Cumulonimbus ), 8.0f )
         << "a cumulonimbus that does not reach the upper troposphere is a congestus";

    std::printf( "[CloudField] species bands (km): stratus %.2f-%.2f  mediocris %.2f-%.2f  "
                 "congestus %.2f-%.2f  cumulonimbus %.2f-%.2f\n",
                 ProfileBaseKm( CloudSpecies::Stratus ), ProfileTopKm( CloudSpecies::Stratus ),
                 ProfileBaseKm( CloudSpecies::CumulusMediocris ), ProfileTopKm( CloudSpecies::CumulusMediocris ),
                 ProfileBaseKm( CloudSpecies::CumulusCongestus ), ProfileTopKm( CloudSpecies::CumulusCongestus ),
                 ProfileBaseKm( CloudSpecies::Cumulonimbus ), ProfileTopKm( CloudSpecies::Cumulonimbus ) );
}

TEST( CloudFieldProfile, ThreeSpeciesAreThreeShapesAndNotThreeLabels )
{
    // THE CRITERION THE WHOLE PHASE EXISTS FOR. A library of species on top of one curve gives nine
    // captions to one cloud; the way to state "these are different clouds" without appealing to a
    // screenshot is that they hold different amounts of cloud in the vertical.
    //
    // Measured at a FULL patch, because that is where a species is most itself, and in kilometre-units so
    // that a species which merely sits higher does not count as a different shape.
    const float stratus      = ProfileIntegralKm( CloudSpecies::Stratus, 1.0f );
    const float congestus    = ProfileIntegralKm( CloudSpecies::CumulusCongestus, 1.0f );
    const float cumulonimbus = ProfileIntegralKm( CloudSpecies::Cumulonimbus, 1.0f );

    std::printf( "[CloudField] profile integrals at a full patch (km): stratus %.3f  congestus %.3f  "
                 "cumulonimbus %.3f\n",
                 stratus, congestus, cumulonimbus );

    // Each is at least half again the one below it. A tolerance rather than "not equal": two shapes that
    // differ in the fourth decimal are two labels, and the show has to be visible.
    EXPECT_GT( congestus, stratus * 1.5f ) << "a congestus holds no more cloud than a stratus";
    EXPECT_GT( cumulonimbus, congestus * 1.5f ) << "a cumulonimbus holds no more cloud than a congestus";

    // And the fourth species is not a duplicate of one of the three either.
    const float mediocris = ProfileIntegralKm( CloudSpecies::CumulusMediocris, 1.0f );
    EXPECT_GT( mediocris, stratus * 1.5f );
    EXPECT_LT( mediocris, congestus * 0.7f );
}

TEST( CloudFieldProfile, AFullerPatchIsATallerCloudOfTheSameSpecies )
{
    // THE SECOND AXIS, and the reason the profile is a table at all. At the rim of a placement patch the
    // species is a flat pad; at its core it is the full thing. Stated as a relation over the axis rather
    // than at two points, because a table that had its axes swapped would still pass a two-point test at
    // some patterns.
    for ( const CloudSpecies species :
          { CloudSpecies::CumulusMediocris, CloudSpecies::CumulusCongestus, CloudSpecies::Cumulonimbus } )
    {
        float previous = -1.0f;
        for ( int step = 0; step <= 20; ++step )
        {
            const float pattern  = 0.05f * static_cast<float>( step );
            const float integral = ProfileIntegralKm( species, pattern );

            EXPECT_GE( integral, previous - 1e-4f )
                 << "species " << static_cast<int>( species ) << " holds LESS cloud at pattern " << pattern
                 << " than at the thinner patch below it";
            previous = integral;
        }

        // And the two ends genuinely differ, or the axis is decoration. The threshold is the one
        // ShapeModel.md §14 asks for by name: the integral at a thin patch is strictly below the one at a
        // full patch.
        EXPECT_LT( ProfileIntegralKm( species, 0.2f ), ProfileIntegralKm( species, 0.9f ) * 0.8f )
             << "species " << static_cast<int>( species )
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
    const CloudSpeciesShape& shape = CloudSpeciesShapeOf( CloudSpecies::Cumulonimbus );

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
    const CloudSpeciesShape& shape = CloudSpeciesShapeOf( CloudSpecies::CumulusCongestus );

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
    for ( const CloudSpecies species : { CloudSpecies::Stratus, CloudSpecies::CumulusMediocris,
                                         CloudSpecies::CumulusCongestus, CloudSpecies::Cumulonimbus } )
    {
        CloudProfileTableSelect( species );

        const CloudSpeciesShape& shape = CloudSpeciesShapeOf( species );

        const float bottomKm = CloudSpeciesBaseKm( shape );
        const float spanKm   = CloudSpeciesTopKm( shape ) - bottomKm;

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
                const float read      = CloudSampleProfileTable( fraction, pattern );

                worst = std::max( worst, std::abs( generated - read ) );
            }
        }

        std::printf( "[CloudField] species %d: worst generator-vs-shader disagreement %.7f\n",
                     static_cast<int>( species ), worst );

        EXPECT_LT( worst, 1e-5f ) << "species " << static_cast<int>( species )
                                  << ": the table the shader reads is not the table the generator wrote";
    }

    CloudProfileTableSelect( CloudSpecies::CumulusCongestus );
}

TEST( CloudFieldProfileTable, TheReadIsClampedSoTheLayerCeilingDoesNotWrapOntoItsFloor )
{
    // EVERY SAMPLER IN THIS ENGINE IS REPEAT (VulkanImage.cpp: addressModeU/V/W are hard-coded), and a
    // profile table is the one kind of image for which that is wrong. Without the clamp inside
    // CloudSampleProfileTable a height fraction of 1 reads the bottom row and the top of the layer grows
    // the cloud's own base back on top of itself — a lid, and one that no coverage setting removes.
    CloudProfileTableSelect( CloudSpecies::CumulusCongestus );

    for ( const float pattern : { 0.0f, 0.5f, 1.0f } )
    {
        // THE CEILING IS EMPTY, always: the top of the envelope is the top of the tallest column this
        // species can grow, and the taper has run out by then. If the read wrapped, this would be reading
        // the FLOOR — which for a cumulus is a flat base at up to a third of full density — and the layer
        // would have a lid made of its own bottom.
        EXPECT_LT( CloudSampleProfileTable( 1.0f, pattern ), 0.02f ) << "the layer ceiling is not empty";

        // Out of range on the altitude axis reads as the nearest edge and NOT as the opposite one. This is
        // the wrap itself: a march that asked for 1.5 with a REPEAT sampler and no clamp would get the
        // value at 0.5.
        EXPECT_FLOAT_EQ( CloudSampleProfileTable( 1.5f, pattern ), CloudSampleProfileTable( 1.0f, pattern ) );
        EXPECT_FLOAT_EQ( CloudSampleProfileTable( -0.5f, pattern ), CloudSampleProfileTable( 0.0f, pattern ) );
    }

    // THE FLOOR IS DELIBERATELY NOT EMPTY, and it would be wrong if it were. The envelope's bottom IS the
    // species' condensation level, a cumulus base is genuinely abrupt, and the first row of the table
    // therefore carries real body — measured at about a third of full density for a thin column, whose
    // base ramp is a fraction of its own small height. A profile that faded in above the shell would be
    // fog, not cloud.
    EXPECT_GT( CloudSampleProfileTable( 0.0f, 0.0f ), 0.1f )
         << "the species' base has become a fade-in, so its clouds have rounded bottoms";

    // And the pattern axis does not wrap either: a pattern past 1 must read as a full patch and not as an
    // empty one.
    EXPECT_NEAR( CloudSampleProfileTable( 0.5f, 1.5f ), CloudSampleProfileTable( 0.5f, 1.0f ), 1e-5f );
    EXPECT_NEAR( CloudSampleProfileTable( 0.5f, -0.5f ), CloudSampleProfileTable( 0.5f, 0.0f ), 1e-5f );
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
                const vec3  positionKm( x, heightFraction * EnvelopeThicknessKm( kDefaultSpecies ), z );

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
        sample.DetailType   = params.DetailCharacter;
        // ONE, not the params' scale: what is measured here is the fraction of the profile that survives
        // the EROSION, and the density scale is a multiplier applied after it. Since the packer folds the
        // species' own density into that scale, leaving it in would make every retention read 1.15 for a
        // congestus and 0.70 for a stratus, which says nothing about erosion at all.
        sample.DensityScale = 1.0f;

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

    const float envelopeKm = EnvelopeThicknessKm( kDefaultSpecies );

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
    // The species decides whether its erosion is wispy or billowy, and it decides it for the whole layer.
    // The scalar it replaced was mixed with a noise per sample, which meant the CHARACTER of an edge
    // changed within one cloud — a stratus rim on one side of a cumulus and a cumulus rim on the other.
    CloudFieldParams params = DefaultParams();

    const float envelopeKm = EnvelopeThicknessKm( kDefaultSpecies );

    for ( int iz = 0; iz < 24; ++iz )
    {
        for ( int ix = 0; ix < 24; ++ix )
        {
            const vec3 position( 8.0f * ( ix + 0.5f ) / 24.0f, 0.4f * envelopeKm, 8.0f * ( iz + 0.5f ) / 24.0f );

            EXPECT_FLOAT_EQ( SampleCloudField( params, 0.4f, position ).DetailType, params.DetailCharacter )
                 << "the erosion character wandered away from the species";
        }
    }
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
