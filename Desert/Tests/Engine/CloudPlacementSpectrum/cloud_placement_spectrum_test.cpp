// CloudPlacementSpectrum — is the sky on a grid, and does the instrument that answers that work?
//
// WHAT IS UNDER TEST, AND IT IS TWO THINGS BECAUSE THE SECOND IS WORTHLESS WITHOUT THE FIRST.
//
//   1. Tools/LatticePeak/Source/LatticePeakMath.hpp — the autocorrelation, the topographic prominence, the
//      lattice score and the jackknife noise. Every number this phase reports was produced by that header,
//      so a fault in it is a fault in every one of them. It is fed fields whose period this file CHOSE.
//
//   2. Engine/Assets/CloudProceduralVolume.cpp's placement, measured with it.
//
// WHY THE PLACEMENT IS MEASURED ON A RASTERISED PROXY AND NOT ONLY ON THE BAKE. A bake is three seconds in
// a Debug build and a lattice measurement needs SEVERAL independent regions, because an autocorrelation
// estimated from sixteen cells across wobbles by about a quarter of its own scale — that is the first thing
// the instrument taught its author and it is written on --repeats. Sixteen bakes is a minute of suite for
// one assertion. So the placement's own output — the lumps, which is where a lattice can be — is rasterised
// into the same top-down quantity the bake produces, and the last test in this file ties the proxy to the
// real thing by measuring one baked volume and demanding the same verdict.
//
// WHAT THE PROXY IS: for each lump, its vertical DIAMETER added over the disc its horizontal radii cover.
// That is the column integral of the lump's bounding cylinder, which is the same quantity — how much cloud
// is above this patch of ground — that Tools/LatticePeak's `--project sum` reads off the baked volume.

#include <LatticePeakMath.hpp>

#include <Engine/Assets/CloudProceduralVolume.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Desert::Assets;

namespace
{
    constexpr int kMapSide = 256;

    /// The shipped congestus, written out here rather than read from the asset layer, because this suite
    /// links no asset layer. Only the numbers the PLACEMENT reads matter to it, and
    /// Desert/Tests/Engine/CloudType is what pins the library against meteorology.
    Desert::Graphic::CloudTypeShape Congestus()
    {
        Desert::Graphic::CloudTypeShape shape{};
        shape.BaseAltitudeKm      = 2.20f;
        shape.TopAltitudeKm       = 5.80f;
        shape.EdgeTopFraction     = 0.15f;
        shape.BaseRampFraction    = 0.04f;
        shape.TopTaper            = 0.50f;
        shape.DetailCharacter     = 1.00f;
        shape.DetailFactor        = 1.00f;
        shape.DensityFactor       = 1.15f;
        shape.ExtinctionFactor    = 1.00f;
        shape.PlacementScale      = 1.00f;
        shape.PlacementAnisotropy = 1.00f;
        return shape;
    }

    /// The shipped layer: a 48 km region, a 3 km cell, the wind along +X so the lattice's axes are the
    /// volume's axes and a per-axis prediction is meaningful.
    CloudProceduralFieldParams ShippedParams()
    {
        CloudProceduralFieldParams params;
        params.RegionSizeKm      = 48.0f;
        params.LayerBottomKm     = 2.20f;
        params.LayerThicknessKm  = 3.60f;
        params.BlendRadiusKm     = 0.06f;
        params.ProfileDepthKm    = 0.36f;
        params.Coverage          = 0.60f;
        params.CoverageContrast  = 1.0f;
        params.Seed              = 1u;
        params.WindAxis          = glm::vec2( 1.0f, 0.0f );
        params.ResolvableChordKm = 0.125f;

        CloudProceduralSpecies species;
        species.Shape      = Congestus();
        species.CellKm     = 3.0f;
        species.Anisotropy = 1.0f;
        params.Species.push_back( species );

        return params;
    }

    /// The lumps of one region, rasterised into the top-down column integral described in the file note.
    /// Periodic, because the region is: a lump against a face contributes to the opposite one, which is
    /// exactly what the bake's wrap does and what makes a circular autocorrelation the right estimator.
    std::vector<float> RasteriseColumns( const std::vector<CloudModellingBlob>& blobs, const glm::vec2& originKm,
                                         float regionKm )
    {
        std::vector<float> map( static_cast<size_t>( kMapSide ) * kMapSide, 0.0f );

        const float perVoxelKm = regionKm / static_cast<float>( kMapSide );

        for ( const CloudModellingBlob& blob : blobs )
        {
            const float radiusKm = std::max( blob.RadiiKm.x, blob.RadiiKm.z );
            const float weight   = 2.0f * blob.RadiiKm.y;

            const float centreX = ( blob.CentreKm.x - originKm.x ) / perVoxelKm;
            const float centreZ = ( blob.CentreKm.z - originKm.y ) / perVoxelKm;
            const float radius  = radiusKm / perVoxelKm;

            const int first = static_cast<int>( std::floor( -radius ) );
            const int last  = static_cast<int>( std::ceil( radius ) );

            for ( int dz = first; dz <= last; ++dz )
                for ( int dx = first; dx <= last; ++dx )
                {
                    if ( static_cast<float>( dx * dx + dz * dz ) > radius * radius )
                        continue;

                    int x = static_cast<int>( std::floor( centreX ) ) + dx;
                    int z = static_cast<int>( std::floor( centreZ ) ) + dz;

                    x = ( ( x % kMapSide ) + kMapSide ) % kMapSide;
                    z = ( ( z % kMapSide ) + kMapSide ) % kMapSide;

                    map[static_cast<size_t>( z ) * kMapSide + x] += weight;
                }
        }

        return map;
    }

    /// The lattice score of a placement, averaged over @p regions independent regions, with the noise the
    /// estimator makes on its own. Both axes are measured and the LARGER is returned, because a lattice
    /// that shows on one axis only is still a lattice — that is what "the clouds go in a row" describes.
    struct LatticeVerdict
    {
        double Score = 0.0;
        double Noise = 0.0;
        double LagKm = 0.0;
    };

    LatticeVerdict MeasurePlacement( const CloudProceduralFieldParams& params, int regions )
    {
        const float perVoxelKm = params.RegionSizeKm / static_cast<float>( kMapSide );

        const glm::vec2 extent  = CloudProceduralCellExtentKm( params, params.Species[0] );
        const int       periodX = static_cast<int>( extent.x / perVoxelKm + 0.5f );
        const int       periodZ = static_cast<int>( extent.y / perVoxelKm + 0.5f );

        const int maxLag = kMapSide / 2;

        std::vector<std::vector<double>> curvesX;
        std::vector<std::vector<double>> curvesZ;

        for ( int region = 0; region < regions; ++region )
        {
            // Disjoint cell sets: each region is four whole periods further along, so no cell is measured
            // twice and the curves being averaged are independent.
            const float     cameraKm = static_cast<float>( region ) * params.RegionSizeKm * 4.0f;
            const glm::vec2 origin   = CloudProceduralRegionOriginKm( params, cameraKm, cameraKm );

            const std::vector<float> map = RasteriseColumns( GenerateCloudProceduralBlobs( params, 0u, origin ),
                                                             origin, params.RegionSizeKm );

            curvesX.push_back( LatticePeak::CircularAutocorrelation( map, kMapSide, kMapSide, true, maxLag ) );
            curvesZ.push_back( LatticePeak::CircularAutocorrelation( map, kMapSide, kMapSide, false, maxLag ) );
        }

        const std::vector<double> rx = LatticePeak::Average( curvesX );
        const std::vector<double> rz = LatticePeak::Average( curvesZ );

        const LatticePeak::Peak peakX = LatticePeak::LatticeScore( rx, periodX, 4 );
        const LatticePeak::Peak peakZ = LatticePeak::LatticeScore( rz, periodZ, 4 );

        const double noiseX = LatticePeak::JackknifeNoise( curvesX, LatticePeak::FirstLagAfterCentralLobe( rx ) );
        const double noiseZ = LatticePeak::JackknifeNoise( curvesZ, LatticePeak::FirstLagAfterCentralLobe( rz ) );

        LatticeVerdict verdict;
        if ( peakX.Prominence >= peakZ.Prominence )
        {
            verdict.Score = peakX.Prominence;
            verdict.LagKm = peakX.Lag * perVoxelKm;
        }
        else
        {
            verdict.Score = peakZ.Prominence;
            verdict.LagKm = peakZ.Lag * perVoxelKm;
        }
        verdict.Noise = std::max( noiseX, noiseZ );
        return verdict;
    }

    /// What fraction of the sky has cloud in the column, on the rasterised proxy.
    double CoverOf( const std::vector<float>& map )
    {
        size_t touched = 0;
        for ( float value : map )
            touched += ( value > 0.0f ) ? 1u : 0u;
        return static_cast<double>( touched ) / static_cast<double>( map.size() );
    }

    double CoverOfPlacement( const CloudProceduralFieldParams& params )
    {
        const glm::vec2 origin = CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );
        return CoverOf(
             RasteriseColumns( GenerateCloudProceduralBlobs( params, 0u, origin ), origin, params.RegionSizeKm ) );
    }
} // namespace

// ===================================================================================================
// PART ONE — THE INSTRUMENT, ON FIELDS WHOSE ANSWER THIS FILE ALREADY KNOWS
// ===================================================================================================

// A curve that only ever falls has no local maximum, so it has no prominence anywhere. This is the whole
// reason prominence and not correlation is the reported quantity: a broad body correlates strongly with
// itself at small lags whether or not there is a lattice, and reporting THAT would call every sky a grid.
TEST( CloudPlacementSpectrum, ADecayingCurveHasNoPeakAtAll )
{
    std::vector<double> decaying( 128 );
    for ( size_t k = 0; k < decaying.size(); ++k )
        decaying[k] = std::exp( -static_cast<double>( k ) / 12.0 );

    const LatticePeak::Peak peak = LatticePeak::StrongestPeak( decaying );

    EXPECT_FALSE( peak.Found ) << "a curve that only falls reported a bump at lag " << peak.Lag
                               << " of prominence " << peak.Prominence
                               << ", so the instrument would find a lattice in a sky that has none";
}

// And the mirror of it: a curve that comes back must be found, at the lag it comes back at, with a
// prominence equal to its height above the trough it climbed out of.
TEST( CloudPlacementSpectrum, ABumpIsFoundWhereItWasPutAndMeasuredAgainstItsOwnTrough )
{
    std::vector<double> curve( 128 );
    for ( size_t k = 0; k < curve.size(); ++k )
        curve[k] = std::exp( -static_cast<double>( k ) / 12.0 );

    // A bump of 0.20 riding on the decay at lag 40, three samples wide.
    curve[39] += 0.10;
    curve[40] += 0.20;
    curve[41] += 0.10;

    const LatticePeak::Peak peak = LatticePeak::StrongestPeak( curve );

    ASSERT_TRUE( peak.Found );
    EXPECT_EQ( peak.Lag, 40 ) << "the bump was put at 40 and found at " << peak.Lag;

    // The trough it climbs out of is the decay's own value just before the bump, which at lag 38 is
    // exp(-38/12) = 0.0421; the bump's top is exp(-40/12) + 0.20 = 0.2358.
    EXPECT_NEAR( peak.Prominence, 0.2358 - 0.0421, 0.005 )
         << "the prominence is not the height above the bracketing trough";
}

// A LATTICE OF POINTS PUTS A BUMP ON EVERY MULTIPLE OF ITS PERIOD, and this is the property the whole
// phase is measured by. The field here is built by this file, so the answer is not in question — what is
// being checked is that LatticeScore finds it and reports which multiple it stands on.
TEST( CloudPlacementSpectrum, APerfectLatticeIsFoundAtItsOwnPeriodAndAtItsHarmonics )
{
    constexpr int period = 16;

    std::vector<float> map( static_cast<size_t>( kMapSide ) * kMapSide, 0.0f );
    for ( int z = 0; z < kMapSide; z += period )
        for ( int x = 0; x < kMapSide; x += period )
            for ( int dz = -2; dz <= 2; ++dz )
                for ( int dx = -2; dx <= 2; ++dx )
                {
                    const int px                                   = ( x + dx + kMapSide ) % kMapSide;
                    const int pz                                   = ( z + dz + kMapSide ) % kMapSide;
                    map[static_cast<size_t>( pz ) * kMapSide + px] = 1.0f;
                }

    const std::vector<double> r =
         LatticePeak::CircularAutocorrelation( map, kMapSide, kMapSide, true, kMapSide / 2 );

    const LatticePeak::Peak score = LatticePeak::LatticeScore( r, period, 4 );

    ASSERT_TRUE( score.Found ) << "a perfect lattice of period " << period
                               << " produced no bump on any multiple of it";
    EXPECT_EQ( score.Lag % period, 0 ) << "the bump found is at lag " << score.Lag
                                       << ", which is not a multiple of the period it was built with";
    EXPECT_GT( score.Prominence, 0.5 ) << "a perfect lattice's bump is faint, so the estimator is wrong";

    std::printf( "[CloudPlacementSpectrum] a perfect lattice of period %d: bump at %d, prominence %.4f\n", period,
                 score.Lag, score.Prominence );
}

// AND A FIELD PLACED WITHOUT A LATTICE MUST NOT PRODUCE ONE. Same number of bodies, same size, placed by a
// hash over the whole map instead of on a grid — if this reported a lattice, every "the lattice is gone"
// in this phase would be unfalsifiable.
TEST( CloudPlacementSpectrum, IndependentlyPlacedBodiesProduceNoLatticeAtAnyPeriod )
{
    constexpr int period = 16;

    std::vector<float> map( static_cast<size_t>( kMapSide ) * kMapSide, 0.0f );

    uint32_t   state = 12345u;
    const auto next  = [&state]()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<int>( ( state >> 8 ) % kMapSide );
    };

    for ( int body = 0; body < ( kMapSide / period ) * ( kMapSide / period ); ++body )
    {
        const int x = next();
        const int z = next();
        for ( int dz = -2; dz <= 2; ++dz )
            for ( int dx = -2; dx <= 2; ++dx )
            {
                const int px                                   = ( x + dx + kMapSide ) % kMapSide;
                const int pz                                   = ( z + dz + kMapSide ) % kMapSide;
                map[static_cast<size_t>( pz ) * kMapSide + px] = 1.0f;
            }
    }

    const std::vector<double> r =
         LatticePeak::CircularAutocorrelation( map, kMapSide, kMapSide, true, kMapSide / 2 );

    const LatticePeak::Peak score = LatticePeak::LatticeScore( r, period, 4 );

    std::printf( "[CloudPlacementSpectrum] the same bodies placed independently: prominence %.4f\n",
                 score.Prominence );

    EXPECT_LT( score.Prominence, 0.05 )
         << "bodies placed by a hash over the whole map reported a lattice of prominence " << score.Prominence
         << " at lag " << score.Lag;
}

// The jackknife noise is what separates a real bump from the estimator's own wobble, so it has to be zero
// when the realisations agree and positive when they do not. Without that property a prominence would be
// reported against a background of nothing.
TEST( CloudPlacementSpectrum, TheNoiseEstimateIsZeroWhenTheRealisationsAgreeAndPositiveWhenTheyDoNot )
{
    std::vector<double> curve( 64 );
    for ( size_t k = 0; k < curve.size(); ++k )
        curve[k] = std::exp( -static_cast<double>( k ) / 8.0 );

    const std::vector<std::vector<double>> identical{ curve, curve, curve, curve };
    EXPECT_NEAR( LatticePeak::JackknifeNoise( identical, 1 ), 0.0, 1e-12 )
         << "four identical realisations disagree about something";

    std::vector<std::vector<double>> disagreeing = identical;
    for ( size_t k = 1; k < curve.size(); ++k )
    {
        disagreeing[0][k] += 0.02;
        disagreeing[2][k] += 0.02;
        disagreeing[1][k] -= 0.02;
        disagreeing[3][k] -= 0.02;
    }

    // The even half is high by 0.02 and the odd half low by 0.02, so half their difference is 0.02.
    EXPECT_NEAR( LatticePeak::JackknifeNoise( disagreeing, 1 ), 0.02, 1e-6 );
}

// Otsu's threshold is what makes the frame mode's cloud/sky split a property of the picture rather than of
// whoever ran the tool. On two well separated populations it has to land between them.
TEST( CloudPlacementSpectrum, OtsuSplitsTwoSeparatedPopulationsBetweenThem )
{
    // 0.10 AND 0.90 RATHER THAN 0.20 AND 0.80, and the reason is a real edge of the routine rather than a
    // taste. The threshold is a histogram BIN, so it is a multiple of 1/255; a population sitting exactly
    // on such a multiple lands on the wrong side of a `>` comparison by one part in ten million of float
    // rounding. 0.20 is exactly 51/255 and did precisely that. Values that are not bin boundaries are what
    // a frame's luminances are, so this is the representative case as well as the safe one.
    std::vector<float> values;
    values.insert( values.end(), 4000, 0.10f );
    values.insert( values.end(), 6000, 0.90f );

    const double threshold = LatticePeak::OtsuThreshold( values );

    // THE THRESHOLD IS THE TOP OF THE BACKGROUND CLASS, not a point strictly between the two — Otsu's
    // split is taken at a histogram BIN, and the bin it names is the last one that belongs below. What has
    // to be true is that comparing with `>` separates the populations, which is how both the tool and this
    // assertion use it.
    size_t below = 0;
    size_t above = 0;
    for ( float value : values )
        ( value > threshold ? above : below )++;

    EXPECT_EQ( below, 4000u ) << "the dim population did not all land below the split at " << threshold;
    EXPECT_EQ( above, 6000u ) << "the bright population did not all land above the split at " << threshold;

    EXPECT_GE( threshold, 0.10 );
    EXPECT_LT( threshold, 0.90 );
}

// ===================================================================================================
// PART TWO — THE PLACEMENT, MEASURED WITH THE INSTRUMENT ABOVE
// ===================================================================================================

// THE HEADLINE OF THE PHASE, AND IT IS A RELATION RATHER THAN A NUMBER: the sky that ships must show no
// lattice, and the sky with the scatter returned to zero must show one. Asserting only the first would go
// green on a placement that produced no cloud at all; asserting both means the measurement is alive.
//
// WHY THE SCATTER AND NOT THE DENSITY IS THE ARM THAT BRINGS IT BACK — measured, and it is the opposite of
// what the shape of the defect suggests. Raising the count of clouds per cell ALONE makes the lattice two
// and a half times stronger, because several small clouds crowded into the middle third of a cell mark
// that cell's site more sharply than one large one did. Docs/Clouds/CALIBRATION.md section RW has the row.
TEST( CloudPlacementSpectrum, TheShippedPlacementHasNoLatticeAndConfiningItAgainBringsOneBack )
{
    constexpr int kRegions = 8;

    const CloudProceduralFieldParams shipped = ShippedParams();

    CloudProceduralFieldParams confined = shipped;
    confined.PlacementScatter           = 0.0f;

    const LatticeVerdict free    = MeasurePlacement( shipped, kRegions );
    const LatticeVerdict lattice = MeasurePlacement( confined, kRegions );

    std::printf( "[CloudPlacementSpectrum] shipped scatter %.2f: lattice %.4f at %.3f km, noise %.4f\n",
                 shipped.PlacementScatter, free.Score, free.LagKm, free.Noise );
    std::printf( "[CloudPlacementSpectrum] scatter returned to 0: lattice %.4f at %.3f km, noise %.4f\n",
                 lattice.Score, lattice.LagKm, lattice.Noise );

    EXPECT_GT( lattice.Score, 0.05 )
         << "a placement confined to its own lattice site produced no measurable lattice, so this test "
            "cannot tell the two apart and its other half proves nothing";

    EXPECT_LT( free.Score, 0.02 ) << "the shipped placement still shows a lattice bump of " << free.Score << " at "
                                  << free.LagKm << " km, which is what this phase removed";

    EXPECT_LT( free.Score * 3.0, lattice.Score )
         << "freeing the placement did not reduce the lattice by even a factor of three";
}

// EVERY ONE OF THE FOUR KNOBS MOVES THE FIELD. The contract's §1.3 forbids a setting that does nothing, and
// a placement parameter is the easiest place in this component to get that wrong, because a sky that is
// already busy looks about the same whatever you do to it. So each is moved on its own and the lumps are
// compared.
TEST( CloudPlacementSpectrum, EveryPlacementKnobChangesTheLumps )
{
    const CloudProceduralFieldParams base   = ShippedParams();
    const glm::vec2                  origin = CloudProceduralRegionOriginKm( base, 0.0f, 0.0f );

    const std::vector<CloudModellingBlob> reference = GenerateCloudProceduralBlobs( base, 0u, origin );
    ASSERT_FALSE( reference.empty() );

    const auto differs = [&]( const CloudProceduralFieldParams& moved, const char* what )
    {
        const std::vector<CloudModellingBlob> other = GenerateCloudProceduralBlobs( moved, 0u, origin );

        if ( other.size() != reference.size() )
            return true;

        for ( size_t k = 0; k < other.size(); ++k )
            if ( other[k].CentreKm != reference[k].CentreKm || other[k].RadiiKm != reference[k].RadiiKm )
                return true;

        ADD_FAILURE() << what << " produced the identical field, so it is a setting that moves nothing";
        return false;
    };

    CloudProceduralFieldParams density = base;
    density.PlacementDensity           = 1.0f;
    EXPECT_TRUE( differs( density, "Cloud Density" ) );

    CloudProceduralFieldParams scatter = base;
    scatter.PlacementScatter           = 0.0f;
    EXPECT_TRUE( differs( scatter, "Cloud Scatter" ) );

    CloudProceduralFieldParams variety = base;
    variety.PlacementSizeVariety       = 0.0f;
    EXPECT_TRUE( differs( variety, "Cloud Size Variety" ) );

    CloudProceduralFieldParams strength = base;
    strength.PatchStrength              = 0.0f;
    EXPECT_TRUE( differs( strength, "Weather Patch Strength" ) );

    CloudProceduralFieldParams tile = base;
    tile.PatchTileKm                = 40.0f;
    EXPECT_TRUE( differs( tile, "Weather Patch Size" ) );
}

// THE DENSITY REDISTRIBUTES MATTER RATHER THAN ADDING IT, and that is a property by construction rather
// than a coincidence: a cell carrying `d` clusters gives each of them one over the square root of `d` of
// the width, so the ground they cover between them is the ground one covered. It is asserted because it is
// what lets the Coverage slider keep its meaning at every setting of the density — without it, decision
// D-20's mapping would have to be re-measured for each one, which is a knob that invalidates another knob.
TEST( CloudPlacementSpectrum, TheDensityDoesNotMoveTheSkysCover )
{
    CloudProceduralFieldParams params = ShippedParams();

    params.PlacementDensity = 1.0f;
    const double one        = CoverOfPlacement( params );

    params.PlacementDensity = 4.0f;
    const double four       = CoverOfPlacement( params );

    std::printf( "[CloudPlacementSpectrum] cover at density 1 / 4: %.4f / %.4f\n", one, four );

    EXPECT_NEAR( one, four, 0.05 ) << "quadrupling the number of clouds per cell moved the sky's cover by "
                                   << std::abs( one - four )
                                   << ", so the density knob and the coverage slider fight each other";
}

// AND SO DOES THE SIZE SPREAD, for the same reason stated differently: the draw is uniform in AREA and not
// in width, so its mean is one whatever the spread. A spread that was uniform in the RADIUS would have
// raised the mean area by a twelfth of the spread squared and taken the coverage mapping with it.
TEST( CloudPlacementSpectrum, TheSizeVarietyDoesNotMoveTheSkysCover )
{
    CloudProceduralFieldParams params = ShippedParams();

    params.PlacementSizeVariety = 0.0f;
    const double none           = CoverOfPlacement( params );

    params.PlacementSizeVariety = 1.0f;
    const double full           = CoverOfPlacement( params );

    std::printf( "[CloudPlacementSpectrum] cover at size variety 0 / 1: %.4f / %.4f\n", none, full );

    EXPECT_NEAR( none, full, 0.05 ) << "spreading the cloud sizes moved the sky's cover by "
                                    << std::abs( none - full );
}

// THE SLIDER STILL MEANS THE SKY AT THE PLACEMENT THAT SHIPS, and this test exists because a sabotage
// found the hole it fills. `CloudProceduralField` already measures the Coverage slider against the sky and
// allows it a TENTH — a bound set for a placement that kept every cluster near its own lattice site, on a
// fixture whose cell is finer than the shipped one. Deleting the packing compensation entirely left that
// suite GREEN: its worst deviation went from 0.033 to 0.050 and a tenth swallowed both.
//
// The compensation is what pays for the free placement's worse packing, so it has to be asserted where it
// bites — at the shipped 3 km cell, near the top of the slider, on the REAL bake. With the compensation the
// sky delivered at 0.75 is 0.741; without it, 0.703.
TEST( CloudPlacementSpectrum, TheCoverageSliderStillMeansTheSkyAtTheShippedPlacement )
{
    double worst = 0.0;

    for ( const float wanted : { 0.35f, 0.75f } )
    {
        CloudProceduralFieldParams params = ShippedParams();
        params.Coverage                   = wanted;

        const glm::vec2 origin = CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );
        const auto      baked  = BakeCloudProceduralVolume( params, origin );
        ASSERT_TRUE( baked ) << ( baked ? std::string{} : baked.GetError() );

        size_t columns = 0;
        for ( uint32_t z = 0; z < kCloudProceduralVolumeDepth; ++z )
            for ( uint32_t x = 0; x < kCloudProceduralVolumeWidth; ++x )
                for ( uint32_t y = 0; y < kCloudProceduralVolumeHeight; ++y )
                {
                    const size_t at = ( ( static_cast<size_t>( z ) * kCloudProceduralVolumeHeight + y ) *
                                             kCloudProceduralVolumeWidth +
                                        x ) *
                                      kCloudProceduralBytesPerVoxel;
                    if ( baked.GetValue()[at] != 0u )
                    {
                        ++columns;
                        break;
                    }
                }

        const double measured = static_cast<double>( columns ) /
                                static_cast<double>( kCloudProceduralVolumeWidth * kCloudProceduralVolumeDepth );

        std::printf( "[CloudPlacementSpectrum] coverage %.2f -> %.3f of the sky (%+.3f)\n", wanted, measured,
                     measured - wanted );

        worst = std::max( worst, std::abs( measured - static_cast<double>( wanted ) ) );
    }

    // A FORTIETH, AND THE NUMBER IS THE GAP RATHER THAN AN AMBITION. With the compensation the worst of the
    // two settings is out by 0.009; with it deleted, by 0.047. Anything between the two separates them, and
    // 0.025 sits in the middle with a factor of two of headroom on each side.
    EXPECT_LT( worst, 0.025 ) << "the Coverage slider is out by " << worst
                              << " of the sky at the shipped placement, which is where the packing the free "
                                 "placement costs has to be paid for";
}

// A PATCH FINER THAN THREE CELLS IS A CHECKERBOARD, NOT A WEATHER SYSTEM, and it is refused by name rather
// than drawn. This is the one relation of the four knobs that has a hard bound on it: the modulation exists
// to decide REGIONS of sky, and one whose period is near a cell's decides cells one at a time.
TEST( CloudPlacementSpectrum, APatchFinerThanThreeCellsIsRefusedAndACoarseOneIsAccepted )
{
    CloudProceduralFieldParams params = ShippedParams();
    params.PatchStrength              = 0.6f;

    const glm::vec2 extent = CloudProceduralCellExtentKm( params, params.Species[0] );
    const float     cellKm = std::max( extent.x, extent.y );

    params.PatchTileKm = 2.9f * cellKm;
    EXPECT_FALSE( ValidateCloudProceduralParams( params ) )
         << "a patch of " << params.PatchTileKm << " km against a cell of " << cellKm
         << " km was accepted, so the modulation may be made as fine as the lattice it modulates";

    params.PatchTileKm = 3.1f * cellKm;
    EXPECT_TRUE( ValidateCloudProceduralParams( params ) );

    // AND IT IS NOT CHECKED WHEN IT IS OFF, because a number nothing reads must not refuse a layer.
    params.PatchStrength = 0.0f;
    params.PatchTileKm   = 0.5f * cellKm;
    EXPECT_TRUE( ValidateCloudProceduralParams( params ) )
         << "a patch size was refused on a layer whose patch modulation is switched off";
}

// THE ANVIL IS PART OF ITS CLUSTER AND HAS TO SHRINK WITH IT, and this test exists because a sabotage found
// that nothing said so. The anvil is a second, wider, flatter lump above the tower, and it is the only lump
// in the generator whose width is written a second time rather than derived from the cluster's radius —
// which is exactly the two-places-that-must-agree shape §2.3.1 of the contract is about. Deleting the
// cluster's own size, density and packing factors from the anvil's line left every suite in the repository
// green: only the cumulonimbus has an anvil at all, and no suite baked one procedurally and looked at it.
//
// What is asserted is the RELATION rather than either number: whatever the density does to the tower it must
// do to the anvil, so the ratio between them cannot depend on the density.
TEST( CloudPlacementSpectrum, TheAnvilShrinksWithTheClusterItCaps )
{
    const auto anvilToTowerAt = [&]( float density )
    {
        CloudProceduralFieldParams params = ShippedParams();
        params.PlacementDensity           = density;

        // The shipped cumulonimbus, which is the only type in the library with an anvil.
        Desert::Graphic::CloudTypeShape& shape = params.Species[0].Shape;
        shape.BaseAltitudeKm                   = 0.9f;
        shape.TopAltitudeKm                    = 9.0f;
        shape.EdgeTopFraction                  = 0.12f;
        shape.TopTaper                         = 0.4f;
        shape.AnvilAltitudeKm                  = 9.5f;
        shape.AnvilThicknessKm                 = 1.8f;
        shape.AnvilStrength                    = 0.85f;

        params.LayerBottomKm     = 0.9f;
        params.LayerThicknessKm  = 10.4f;
        params.Species[0].CellKm = 6.0f;

        const glm::vec2                       origin = CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );
        const std::vector<CloudModellingBlob> blobs  = GenerateCloudProceduralBlobs( params, 0u, origin );

        double anvil  = 0.0;
        double anvils = 0.0;
        double tower  = 0.0;
        double towers = 0.0;

        for ( const CloudModellingBlob& blob : blobs )
        {
            // The anvil is the lump AT the anvil altitude; every other lump of a cluster sits inside the
            // band between the type's base and top, which ends half a kilometre below it.
            if ( std::abs( blob.CentreKm.y - shape.AnvilAltitudeKm ) < 1e-3f )
            {
                anvil += blob.RadiiKm.x;
                anvils += 1.0;
            }
            else
            {
                tower += blob.RadiiKm.x;
                towers += 1.0;
            }
        }

        EXPECT_GT( anvils, 0.0 ) << "no anvil was generated at all, so this test measured nothing";
        EXPECT_GT( towers, 0.0 );

        return ( anvil / std::max( anvils, 1.0 ) ) / ( tower / std::max( towers, 1.0 ) );
    };

    const double one  = anvilToTowerAt( 1.0f );
    const double four = anvilToTowerAt( 4.0f );

    std::printf( "[CloudPlacementSpectrum] anvil/tower width at density 1 / 4: %.4f / %.4f\n", one, four );

    EXPECT_NEAR( one, four, 0.05 * one )
         << "the anvil and the tower it caps scale differently with the density, so at one setting the "
            "anvil is a lid the storm has outgrown and at another it is a saucer floating over a stump";
}

// THE CACHE IS THE OTHER PLACE A LIVE SETTING CAN DIE, and this test exists because a sabotage found it
// green. Every field is wired from the component to the bake, and the bake reads every one of them — and
// none of that matters if the renderer's staleness check does not NOTICE the change, because then the
// artist moves the slider and the volume it already has is handed back. That is a dead setting arrived at
// from the far side, and it looks exactly like the knob not being wired.
//
// The check used to live in the renderer's own translation unit, which nothing links, so dropping a field
// from it broke nothing that could go red. It is Assets::CloudProceduralParamsEqual now, and this walks
// every field of the struct in turn.
TEST( CloudPlacementSpectrum, EveryFieldTheBakeReadsMakesTheCachedVolumeStale )
{
    const CloudProceduralFieldParams base = ShippedParams();

    EXPECT_TRUE( CloudProceduralParamsEqual( base, base ) )
         << "a set of parameters is not equal to itself, so this test proves nothing about the rest";

    const auto notices = [&]( CloudProceduralFieldParams moved, const char* what )
    {
        EXPECT_FALSE( CloudProceduralParamsEqual( base, moved ) )
             << what
             << " was changed and the cached volume was still considered current, so moving it in "
                "the editor would do nothing at all";
    };

    CloudProceduralFieldParams moved = base;
    moved.RegionSizeKm               = 40.0f;
    notices( moved, "Region Size" );

    moved               = base;
    moved.LayerBottomKm = 1.0f;
    notices( moved, "the layer's base altitude" );

    moved                  = base;
    moved.LayerThicknessKm = 2.0f;
    notices( moved, "the layer's thickness" );

    moved               = base;
    moved.BlendRadiusKm = 0.09f;
    notices( moved, "the blend radius" );

    moved                = base;
    moved.ProfileDepthKm = 0.50f;
    notices( moved, "the profile depth" );

    moved          = base;
    moved.Coverage = 0.30f;
    notices( moved, "Coverage" );

    moved                  = base;
    moved.CoverageContrast = 2.0f;
    notices( moved, "Coverage Contrast" );

    moved      = base;
    moved.Seed = 7u;
    notices( moved, "Seed" );

    moved          = base;
    moved.WindAxis = glm::vec2( 0.0f, 1.0f );
    notices( moved, "the wind axis" );

    moved                   = base;
    moved.ResolvableChordKm = 0.25f;
    notices( moved, "the march's resolvable chord" );

    moved                  = base;
    moved.PlacementDensity = 1.0f;
    notices( moved, "Cloud Density" );

    moved                  = base;
    moved.PlacementScatter = 0.0f;
    notices( moved, "Cloud Scatter" );

    moved                      = base;
    moved.PlacementSizeVariety = 0.0f;
    notices( moved, "Cloud Size Variety" );

    moved               = base;
    moved.PatchStrength = 0.0f;
    notices( moved, "Weather Patch Strength" );

    moved             = base;
    moved.PatchTileKm = 40.0f;
    notices( moved, "Weather Patch Size" );

    moved                   = base;
    moved.Species[0].CellKm = 4.0f;
    notices( moved, "the species' cell" );

    moved                       = base;
    moved.Species[0].Anisotropy = 2.0f;
    notices( moved, "the species' anisotropy" );

    moved                                  = base;
    moved.Species[0].Shape.EdgeTopFraction = 0.5f;
    notices( moved, "the type's edge top fraction" );

    moved = base;
    moved.Species.push_back( moved.Species[0] );
    notices( moved, "a second species" );
}

// The density's ceiling is the bake's cost and is refused rather than survived: the cost is linear in the
// lump count, and a mistyped density is a bake that never returns rather than a sky that looks wrong.
TEST( CloudPlacementSpectrum, ThePlacementNumbersAreRefusedOutsideTheirRanges )
{
    CloudProceduralFieldParams params = ShippedParams();

    params                  = ShippedParams();
    params.PlacementDensity = 250.0f;
    EXPECT_FALSE( ValidateCloudProceduralParams( params ) ) << "a density of 250 clusters per cell was "
                                                               "accepted; that is a bake of a million lumps";

    params                  = ShippedParams();
    params.PlacementScatter = -1.0f;
    EXPECT_FALSE( ValidateCloudProceduralParams( params ) );

    params                      = ShippedParams();
    params.PlacementSizeVariety = 2.0f;
    EXPECT_FALSE( ValidateCloudProceduralParams( params ) );

    params               = ShippedParams();
    params.PatchStrength = 1.5f;
    EXPECT_FALSE( ValidateCloudProceduralParams( params ) );
}

// ===================================================================================================
// PART THREE — THE PROXY AGAINST THE REAL THING
// ===================================================================================================
//
// Everything in part two measures the rasterised column integral of the LUMPS, because a bake is seconds
// and a lattice measurement wants several regions. This is the one test that pays for a bake, and its whole
// job is to stop that proxy from drifting away from what it stands for: the same verdict, measured on the
// baked volume's own top-down projection, on the configuration that ships.
TEST( CloudPlacementSpectrum, TheBakedVolumeAgreesWithTheProxyThatTheShippedSkyHasNoLattice )
{
    const CloudProceduralFieldParams params = ShippedParams();

    const glm::vec2 origin = CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );
    const auto      baked  = BakeCloudProceduralVolume( params, origin );
    ASSERT_TRUE( baked ) << ( baked ? std::string{} : baked.GetError() );

    const uint32_t width  = kCloudProceduralVolumeWidth;
    const uint32_t height = kCloudProceduralVolumeHeight;
    const uint32_t depth  = kCloudProceduralVolumeDepth;

    std::vector<float> map( static_cast<size_t>( width ) * depth, 0.0f );
    for ( uint32_t z = 0; z < depth; ++z )
        for ( uint32_t x = 0; x < width; ++x )
        {
            float sum = 0.0f;
            for ( uint32_t y = 0; y < height; ++y )
            {
                const size_t at =
                     ( ( static_cast<size_t>( z ) * height + y ) * width + x ) * kCloudProceduralBytesPerVoxel;
                sum += static_cast<float>( baked.GetValue()[at] ) * ( 1.0f / 255.0f );
            }
            map[static_cast<size_t>( z ) * width + x] = sum / static_cast<float>( height );
        }

    const float     perVoxelKm = params.RegionSizeKm / static_cast<float>( width );
    const glm::vec2 extent     = CloudProceduralCellExtentKm( params, params.Species[0] );
    const int       period     = static_cast<int>( extent.x / perVoxelKm + 0.5f );

    const std::vector<double> r = LatticePeak::CircularAutocorrelation(
         map, static_cast<int>( width ), static_cast<int>( depth ), true, static_cast<int>( width ) / 2 );

    const LatticePeak::Peak score = LatticePeak::LatticeScore( r, period, 4 );

    std::printf( "[CloudPlacementSpectrum] the BAKED volume, one region: lattice %.4f (period %.3f km)\n",
                 score.Prominence, extent.x );

    // ONE REGION IS SIXTEEN CELLS ACROSS and an autocorrelation from sixteen things wobbles, so the bound
    // here is looser than part two's by exactly that: it is a check that the proxy has not drifted, not a
    // second measurement of the phase.
    EXPECT_LT( score.Prominence, 0.06 )
         << "the baked volume shows a lattice bump of " << score.Prominence << " at lag " << score.Lag
         << " where the proxy the rest of this suite measures says there is none — the two have drifted "
            "apart and the proxy can no longer be trusted";
}

// ---------------------------------------------------------------------------------------------------
// PART THREE — THE SHAPE OF A BODY, WHICH THE PEAK IS BLIND TO
// ---------------------------------------------------------------------------------------------------
//
// The autocorrelation answers where the bodies are. Two skies with identical placement — one of towers and
// one of plates standing on the same footprints — give the same curve and the same prominence, so "the
// lattice is gone" and "the sky looks natural" are different claims and only the first of them has been
// measurable in this programme so far. LatticePeak::AccumulateChords is the second: how far a ray running
// along an axis stays inside cloud, gathered over every scan line of the baked volume.

TEST( CloudPlacementSpectrum, AChordCensusCountsEveryUnbrokenRunAndNothingElse )
{
    LatticePeak::ChordCensus census;

    // Two runs, of one and of two.
    LatticePeak::AccumulateChords( { 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f }, false, census );

    EXPECT_EQ( census.Runs, 2u );
    EXPECT_NEAR( census.MeanVoxels(), 1.5, 1e-9 );

    // An empty line adds nothing at all rather than a run of zero, which would drag the mean down.
    LatticePeak::AccumulateChords( { 0.0f, 0.0f, 0.0f }, false, census );
    EXPECT_EQ( census.Runs, 2u );
    EXPECT_NEAR( census.MeanVoxels(), 1.5, 1e-9 );

    // An empty vector is not a line and must not fault.
    LatticePeak::AccumulateChords( {}, true, census );
    EXPECT_EQ( census.Runs, 2u );
}

// THE WRAP IS THE DIFFERENCE BETWEEN ONE BODY AND TWO, and this is the assertion that says so. The baked
// volume is exactly periodic across the region's faces, so a body straddling a face is the same body seen
// twice; counted without the wrap it arrives as two short chords and the mean falls.
TEST( CloudPlacementSpectrum, TheWrapJoinsABodyThatStraddlesTheRegionsFace )
{
    const std::vector<float> line{ 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    LatticePeak::ChordCensus wrapped;
    LatticePeak::AccumulateChords( line, true, wrapped );
    EXPECT_EQ( wrapped.Runs, 1u );
    EXPECT_NEAR( wrapped.MeanVoxels(), 3.0, 1e-9 );

    LatticePeak::ChordCensus cut;
    LatticePeak::AccumulateChords( line, false, cut );
    EXPECT_EQ( cut.Runs, 2u );
    EXPECT_NEAR( cut.MeanVoxels(), 1.5, 1e-9 );

    // A line that is cloud from end to end is ONE body that circles the world, not two and not none.
    LatticePeak::ChordCensus solid;
    LatticePeak::AccumulateChords( { 1.0f, 1.0f, 1.0f, 1.0f }, true, solid );
    EXPECT_EQ( solid.Runs, 1u );
    EXPECT_NEAR( solid.MeanVoxels(), 4.0, 1e-9 );
}

// THE SPAN IS NOT THE CHORD, and a suite that measured only one of them would call a tower with air in it
// a plate. Both are needed to say what the sky is made of, so both are asserted.
TEST( CloudPlacementSpectrum, ASpanReachesOverAGapAndAChordStopsAtIt )
{
    const std::vector<float> tower{ 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    EXPECT_EQ( LatticePeak::OccupiedSpan( tower ), 4u );

    LatticePeak::ChordCensus census;
    LatticePeak::AccumulateChords( tower, false, census );
    EXPECT_EQ( census.Runs, 2u );
    EXPECT_NEAR( census.MeanVoxels(), 1.0, 1e-9 );

    // A clear column has no span at all rather than a span of one, or the mean over the sky would be
    // dragged towards the empty half of it.
    EXPECT_EQ( LatticePeak::OccupiedSpan( { 0.0f, 0.0f, 0.0f } ), 0u );
    EXPECT_EQ( LatticePeak::OccupiedSpan( {} ), 0u );

    // A solid body's span IS its chord, which is what makes the ratio of the two a measure of solidity.
    EXPECT_EQ( LatticePeak::OccupiedSpan( { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f } ), 3u );
}

// THE RELATION, AND IT IS THE ONE §2.3.1 ASKS FOR: the two chords are set by two DIFFERENT numbers, and a
// change to one must move one of them and not the other. The horizontal chord follows the placement cell —
// the cluster's radius is a fraction of it — and the vertical chord follows the type's own band. Sizing the
// cluster off the band, or the stack off the cell, would tie them together and this test is what would say
// so.
//
// It is also where the number the owner was shown comes from: the shipped congestus is measurably WIDER
// THAN IT IS TALL, and by how much is Docs/Clouds/CALIBRATION.md section RW2.
TEST( CloudPlacementSpectrum, TheBodysWidthFollowsTheCellAndItsHeightFollowsTheBand )
{
    const auto measure =
         []( const CloudProceduralFieldParams& params, double& horizontalKm, double& verticalKm, double& spanKm )
    {
        const glm::vec2 origin = CloudProceduralRegionOriginKm( params, 0.0f, 0.0f );
        const auto      baked  = BakeCloudProceduralVolume( params, origin );
        ASSERT_TRUE( baked ) << ( baked ? std::string{} : baked.GetError() );

        const uint32_t width  = kCloudProceduralVolumeWidth;
        const uint32_t height = kCloudProceduralVolumeHeight;
        const uint32_t depth  = kCloudProceduralVolumeDepth;

        LatticePeak::ChordCensus horizontal;
        LatticePeak::ChordCensus vertical;
        std::vector<float>       line;

        for ( uint32_t y = 0; y < height; ++y )
            for ( uint32_t z = 0; z < depth; ++z )
            {
                line.assign( width, 0.0f );
                for ( uint32_t x = 0; x < width; ++x )
                {
                    const size_t at =
                         ( ( static_cast<size_t>( z ) * height + y ) * width + x ) * kCloudProceduralBytesPerVoxel;
                    line[x] = static_cast<float>( baked.GetValue()[at] );
                }
                LatticePeak::AccumulateChords( line, true, horizontal );
            }

        double spanVoxels  = 0.0;
        size_t spanColumns = 0;

        for ( uint32_t z = 0; z < depth; ++z )
            for ( uint32_t x = 0; x < width; ++x )
            {
                line.assign( height, 0.0f );
                for ( uint32_t y = 0; y < height; ++y )
                {
                    const size_t at =
                         ( ( static_cast<size_t>( z ) * height + y ) * width + x ) * kCloudProceduralBytesPerVoxel;
                    line[y] = static_cast<float>( baked.GetValue()[at] );
                }

                LatticePeak::AccumulateChords( line, false, vertical );

                const size_t span = LatticePeak::OccupiedSpan( line );
                if ( span > 0 )
                {
                    spanVoxels += static_cast<double>( span );
                    ++spanColumns;
                }
            }

        const double perVoxelUpKm = static_cast<double>( params.LayerThicknessKm ) / static_cast<double>( height );

        horizontalKm = horizontal.MeanVoxels() *
                       ( static_cast<double>( params.RegionSizeKm ) / static_cast<double>( width ) );
        verticalKm = vertical.MeanVoxels() * perVoxelUpKm;
        spanKm = ( spanColumns > 0 ) ? ( spanVoxels / static_cast<double>( spanColumns ) ) * perVoxelUpKm : 0.0;
    };

    double shippedWide = 0.0;
    double shippedTall = 0.0;
    double shippedSpan = 0.0;
    measure( ShippedParams(), shippedWide, shippedTall, shippedSpan );

    std::printf( "[CloudPlacementSpectrum] the shipped congestus: chord %.3f km across, %.3f km up, "
                 "%.2fx wider than tall; column span %.3f km, solidity %.2f\n",
                 shippedWide, shippedTall, shippedWide / shippedTall, shippedSpan, shippedTall / shippedSpan );

    EXPECT_GT( shippedWide, shippedTall )
         << "the shipped type is no longer wider than it is tall — section RW2 of "
            "Docs/Clouds/CALIBRATION.md is written against a body that is, and the number it quotes has to "
            "be re-measured";

    // HALVE THE CELL AND THE BODY NARROWS; THE BAND IS UNTOUCHED AND SO IS THE HEIGHT. Anything else means
    // the two have been tied to one number.
    CloudProceduralFieldParams narrow = ShippedParams();
    narrow.Species[0].CellKm *= 0.5f;

    double narrowWide = 0.0;
    double narrowTall = 0.0;
    double narrowSpan = 0.0;
    measure( narrow, narrowWide, narrowTall, narrowSpan );

    std::printf( "[CloudPlacementSpectrum] at half the cell: chord %.3f km across, %.3f km up, span %.3f km\n",
                 narrowWide, narrowTall, narrowSpan );

    EXPECT_LT( narrowWide, shippedWide * 0.90 )
         << "halving the placement cell left the bodies as wide as before, so the cluster's width no "
            "longer follows the cell it is placed in";
    EXPECT_NEAR( narrowTall, shippedTall, shippedTall * 0.25 )
         << "halving the placement cell changed how TALL the bodies are, so the stack has been tied to the "
            "cell instead of to the type's own band";
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
