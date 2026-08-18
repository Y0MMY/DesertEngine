#pragma once

#include <Engine/Graphic/Clouds/CloudMarchScale.hpp>
#include <Engine/Graphic/Clouds/CloudWeatherScale.hpp>

#include <algorithm>

// THE FOURTH SCALE RELATION: a coverage ISLAND must stay larger than the finest erosion the march can
// still carry at the distance the deck is drawn to.
//
// WHAT THE OTHER THREE LEFT OPEN. CloudWeatherScale.hpp ties the weather tile to the layer's altitude,
// CloudMarchScale.hpp ties the march's stride to the layer's thickness, CloudLayerAspect.hpp ties the
// layer's own width to its own height. Every one of those is satisfied by a sky made of boxes, and for a
// whole programme that is what this renderer drew: an owner reported "the sky sits too close to the
// ground", it reproduced on every scene, and the recorded cause — the step schedule sampling small cells
// several times over — was wrong. A converged ground-truth probe (768 uniform samples over the shell, no
// empty-space state machine, no dither, cheap density only) renders THE SAME BOXES, which kills every
// march hypothesis at once. Knocking out budget exhaustion, the temporal resolve, resolution, the
// dither, the coarse skip and the distance softening one at a time leaves them unchanged. Exactly one
// knock-out moves the picture, and it moves it the other way: turning the DETAIL EROSION off makes the
// NEAR clouds boxes too.
//
// THE MECHANISM, stated with the numbers. The far-field density is `coverage(x,z) x verticalProfile x
// baseShape`. Coverage is a TWO-DIMENSIONAL field, so the silhouette is a vertical extrusion with
// vertical sides; the profile puts every cell's base within 3.3% and every cell's top within 16.5% of
// the layer, so the extrusion has a flat base and a flat top. Measured on a 5 km / 2.28 km layer that is
// a box 2.0 km wide by 2.3 km tall. A cube. The only thing that has ever made a coverage island read as
// a cloud is the detail erosion, and CloudNyquistWeight switches that off by design wherever the march
// cannot resolve it — with a 4 km detail tile at the High tier the coarse channel pair (781 m) is gone
// by 47 km, and past that the density carries the noise's mean: the raw box.
//
// So the quantity that decides whether a distant cloud is a cloud at all is the ratio between the size
// of an ISLAND of coverage and the size of the finest erosion feature the march can still carry out
// where that island is drawn. Nothing expressed it. This file does.
//
// TWO CONSEQUENCES, and they are the reason this is worth a header rather than a comment:
//
//   1. A FLOOR UNDER THE LAYER'S ALTITUDE. Island size is proportional to the weather tile and the tile
//      is proportional to the layer's mid altitude, so requiring the island to survive to where the
//      erosion dies puts a MINIMUM on the altitude of any BROKEN sky. At a 4 km detail tile and the High
//      tier that floor is about 3.7 km of base altitude for the cumulus aspect — which is where UE's
//      5 km LayerBottomAltitude default comes from, and why moving the layer up buys a believable sky
//      where shrinking the cell cannot.
//   2. THE DECK MUST END WHERE ITS OWN DETAIL ENDS, not at the planet's geometric horizon. That half is
//      implemented in the shader — see CloudAutoFadeEnd in Common/CloudGeometry.glslh.
//
// ---- THE MIRROR QUESTION, answered honestly rather than by ritual ---------------------------------
//
// CloudLayerAspect.hpp states the rule this file follows: a mirror is a GUARD against two spellings of
// one formula drifting apart, and a second copy with only one reader is the defect shape rather than
// protection from it. This relation splits cleanly down that line, and the split is stated here so the
// next reader does not have to work it out:
//
//   * THE PART THE SHADER READS — the step schedule's inverse, the distance at which the coarse erosion
//     pair stops being carried, and the erosion feature the march carries at a given distance. The
//     raymarch needs those to end the deck (CloudAutoFadeEnd / CloudAutoHorizonStart), and this file
//     needs them to decide. So they are MIRRORS of Common/CloudGeometry.glslh, and the CloudMath suite
//     compiles that header as C++ beside this one and asserts the two agree to the bit over a sweep of
//     tiles and tiers. Change one and that test fails.
//   * THE PART NOBODY ON THE GPU READS — the island size, the predicate, the applicability clause and
//     the tile a scene should have had. Those exist for the renderer's warning and for the preset suite.
//     The march never needs to know whether its own deck is resolvable; it just marches it. So they are
//     stated ONCE, here, in C++, with no GLSL twin.
//
// ---- APPLICABILITY, in code and not only in prose --------------------------------------------------
//
// A SHEET HAS NO ISLANDS. Above roughly Coverage 0.90 the coverage field is CONNECTED: there is no
// isolated blob for the eye to read as a box, the silhouette is the layer's own edge rather than a
// cell's, and this relation says nothing at all. Stratus (0.90), Overcast (0.95) and Storm (0.98) are
// sheets, and a warning that fired on them would be a warning about nothing — which is how a warning
// stops being read. The clause is therefore part of the predicate below, not a note beside it.

namespace Desert::Graphic
{
    // ---- The island ---------------------------------------------------------------------------------

    // How many islands sit across one DOMINANT coverage cell. The weather FBM starts at
    // kCloudWeatherBasePeriod cells across a tile, and at the coverages the presets ship a cell that is
    // above the coverage threshold occupies about half its own period — the other half is the gap that
    // makes the sky broken rather than overcast. So an island is half a cell, and the whole chain from
    // the authored tile to the thing the eye reads as one cloud is `tile / (BASE_PERIOD * 2)`.
    inline constexpr float kCloudIslandsPerCell = 2.0f;

    // The size of one coverage island, in world units (centimetres), for a layer tiled at @p
    // weatherTileSize. This is the thing that has to read as a cloud.
    inline constexpr float CloudDeckIslandSize( float weatherTileSize )
    {
        return std::max( weatherTileSize, 0.0f ) / ( kCloudWeatherBasePeriod * kCloudIslandsPerCell );
    }

    // ---- The erosion, mirrored from the shader ------------------------------------------------------

    // The C++ mirror of CLOUD_DETAIL_EROSION_LOW_PER_TILE (Common/CloudNoise.glslh): how many cells of
    // the COARSE erosion channel pair fit across one detail tile, 32 * 4 / 25 from the amplitude-weighted
    // mean feature size of a three-octave FBM at base period 4. The coarse pair and not the fine one
    // because it is the one that outlives the other — it stays resolvable for twice the distance, so it
    // is the LAST erosion a receding deck has, and where it goes the silhouette goes with it.
    inline constexpr float kCloudErosionCoarseCellsPerTile = 5.12f;

    // The finest erosion feature the march can carry IN FULL at distance @p t: CloudNyquistWeight's own
    // "fully carried where S <= featureSize / 4", solved for the feature at the stride the schedule takes
    // there. Mirrors CloudErosionLodTile's numerator in Common/CloudGeometry.glslh.
    inline float CloudCarriedErosionFeature( float t, float minStep, float maxStep, float growth )
    {
        return 4.0f * CloudStepLengthAt( t, minStep, maxStep, growth );
    }

    // Iterations of the schedule's inverse. Mirrors CLOUD_SCHEDULE_INVERSE_ITERATIONS; see the shader for
    // why sixteen (a 150 km bracket halved to 2.3 m, against fades measured in kilometres).
    inline constexpr int kCloudScheduleInverseIterations = 16;

    // The distance at which the march's stride first reaches @p stride, bounded by @p maxDistance.
    // Mirrors CloudScheduleDistanceForStride, expression for expression — the schedule has no closed-form
    // inverse (a sqrt law blended into a linear one) but is monotone nondecreasing in t, so a bisection
    // is exact to its own resolution and needs no case analysis.
    inline float CloudScheduleDistanceForStrideAt( float stride, float minStep, float maxStep, float growth,
                                                   float maxDistance )
    {
        float hi = std::max( maxDistance, 0.0f );
        if ( CloudStepLengthAt( hi, minStep, maxStep, growth ) <= stride )
            return hi;
        if ( CloudStepLengthAt( 0.0f, minStep, maxStep, growth ) >= stride )
            return 0.0f;

        float lo = 0.0f;
        for ( int i = 0; i < kCloudScheduleInverseIterations; ++i )
        {
            const float mid = 0.5f * ( lo + hi );
            if ( CloudStepLengthAt( mid, minStep, maxStep, growth ) >= stride )
                hi = mid;
            else
                lo = mid;
        }
        return hi;
    }

    // Where the coarse erosion pair stops being carried AT ALL — CloudNyquistWeight's zero, S >= F/2 —
    // and therefore how far this deck can be drawn before it is a field of extruded coverage cells.
    // Mirrors CloudErosionCarriedAtAll.
    inline float CloudErosionCarryDistance( float detailTileSize, float minStep, float maxStep, float growth,
                                            float maxDistance )
    {
        const float feature = std::max( detailTileSize, 1.0f ) / kCloudErosionCoarseCellsPerTile;
        return CloudScheduleDistanceForStrideAt( 0.5f * feature, minStep, maxStep, growth, maxDistance );
    }

    // The distance the deck is actually drawn to: the nearer of where its erosion dies and where the
    // march is clipped. The same number CloudAutoFadeEnd returns on the GPU, which is what makes the
    // predicate below a statement about the picture rather than about an ideal.
    inline float CloudDeckDrawDistance( float detailTileSize, float minStep, float maxStep, float growth,
                                        float maxViewDistance )
    {
        return CloudErosionCarryDistance( detailTileSize, minStep, maxStep, growth, maxViewDistance );
    }

    // ---- The relation -------------------------------------------------------------------------------

    // THE APPLICABILITY CLAUSE ITSELF — kCloudConnectedCoverage and CloudCoverageFieldIsConnected — lives
    // in CloudWeatherScale.hpp, which owns the weather field, and is used unchanged here. Two relations
    // ask the same question of the coverage ("is this a field of islands or a blanket?") and there is one
    // answer; a second literal 0.90 in this file would be the drift these headers exist to prevent.

    // The smallest weather tile whose islands survive to the distance this deck is drawn to — the number
    // a scene should have had, which is what the renderer's warning prints. Inverts
    // `island >= carried feature` through CloudDeckIslandSize.
    inline float CloudDeckMinWeatherTileSize( float detailTileSize, float minStep, float maxStep, float growth,
                                              float maxViewDistance )
    {
        const float drawTo = CloudDeckDrawDistance( detailTileSize, minStep, maxStep, growth, maxViewDistance );
        return CloudCarriedErosionFeature( drawTo, minStep, maxStep, growth ) *
               ( kCloudWeatherBasePeriod * kCloudIslandsPerCell );
    }

    // THE PREDICATE, in the shape CloudWeatherTileIsPlausible and CloudCoarseStrideIsPlausible already
    // established: true when the sky works, and the renderer says out loud what it should have been when
    // it does not.
    //
    // NO TOLERANCE BAND on this one, unlike the weather tile's. That band exists because the tile is
    // over-determined by three relations and something has to give; this is a resolution limit — either
    // the march can still sculpt the island or it cannot — and there is nothing to trade it against.
    inline bool CloudDeckIsResolvable( float coverage, float weatherTileSize, float detailTileSize, float minStep,
                                       float maxStep, float growth, float maxViewDistance )
    {
        if ( CloudCoverageFieldIsConnected( coverage ) )
            return true;

        const float drawTo = CloudDeckDrawDistance( detailTileSize, minStep, maxStep, growth, maxViewDistance );
        return CloudDeckIslandSize( weatherTileSize ) >=
               CloudCarriedErosionFeature( drawTo, minStep, maxStep, growth );
    }

    // ---- What the predicate collapses to, and why it is worth knowing -------------------------------
    //
    // Where MaxViewDistance does NOT clip the deck — every shipped scene — the draw distance is the
    // erosion's own death, the stride there is exactly `feature / 2` by construction, and the carried
    // feature is therefore exactly `2 * DetailTileSize / kCloudErosionCoarseCellsPerTile`, independent of
    // the quality tier. The whole relation reduces to `WeatherTileSize >= 32 * DetailTileSize / 5.12`,
    // i.e. a broken sky needs a coverage tile about six and a quarter times its detail tile. The tier
    // drops out because a cheaper tier ends the deck sooner in exactly the proportion its stride is
    // coarser — which is why the CloudMath suite pins the collapse as a property rather than as a value:
    // it is the strongest single statement this file makes, and it would be the first thing to break if
    // the schedule or the gate were retuned independently of each other.
} // namespace Desert::Graphic
