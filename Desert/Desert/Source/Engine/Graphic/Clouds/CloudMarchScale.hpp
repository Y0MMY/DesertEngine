#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

// The relation between a layer's THICKNESS and the stride the empty-space search takes through it —
// the second scale relation this renderer needs and the one a THIN layer walks straight into.
//
// WHAT IT IS FOR. The march skips empty sky at CoarseStepMultiplier times the fine stride, and only slows
// down once a sample comes back non-empty. A layer whose own chord is comparable with that stride can be
// STEPPED OVER: whether a given ray notices the cloud at all becomes a function of that ray's dither
// phase, i.e. a per-pixel coin toss. The temporal stage cannot rescue it, because the average of "half
// the rays missed the cloud" is "half the cloud" — it converges to a stipple rather than removing one.
//
// WHAT THIS IS NOT. A cross-hatch on a 1.2 km cirrus sheet is what led to this file, and lowering the
// sheet's coarse stride did remove it — but that was the stride shrinking a DIFFERENT quantity. The
// cross-hatch was the march's dithered start deleting a per-pixel prefix of every segment, of length
// `jitter * coarseStride`; see CloudMarchBegin in Editor/Resources/Shaders/Common/CloudGeometry.glslh for
// the mechanism, the measurement and the fix, and CloudMath's two dither-phase properties for the
// assertions. Halving the coarse stride halved the deleted prefix, which is why it looked like this
// relation and was not.
//
// The relation below is real all the same, and it is the one this file is for: a stride comparable with
// the chord lets the SEARCH miss the layer outright, which no dither fixes because there is nothing to
// average. It is what the Low tier does to a thin high sheet.
//
// The reasoning and the SCHEDULE itself live in Editor/Resources/Shaders/Common/CloudGeometry.glslh
// (CloudStepLength). This is its C++ mirror, for the one place that has to decide rather than consume:
// the warning the renderer prints. A mirror of a formula is exactly the shape of defect this project has
// paid for before (RESEARCH_REFERENCE: "a C++ mirror of a gradient vs the shader's own formula"), so the
// CloudMath suite compiles the .glslh as C++ beside this header and asserts the two agree to the bit over
// a sweep of distances and tiers. Change one and that test fails.

namespace Desert::Graphic
{
    // The near/far split of the step schedule, in world units. Mirrors CLOUD_STEP_FINE_RANGE and
    // CLOUD_STEP_FAR_RANGE.
    inline constexpr float kCloudStepFineRange = 1000000.0f; // 10 km
    inline constexpr float kCloudStepFarRange  = 2500000.0f; // 25 km

    // The stride the march takes at distance @p t. Mirrors CloudStepLength, expression for expression.
    inline float CloudStepLengthAt( float t, float minStep, float maxStep, float growth )
    {
        const float linear = minStep + growth * t;
        const float fine =
             std::min( minStep * ( 1.0f + std::sqrt( std::max( t, 0.0f ) / kCloudStepFineRange ) ), linear );
        const float w =
             std::clamp( ( t - kCloudStepFineRange ) / ( kCloudStepFarRange - kCloudStepFineRange ), 0.0f, 1.0f );
        const float blended = fine + ( linear - fine ) * w;
        return std::clamp( blended, std::min( minStep, maxStep ), std::max( minStep, maxStep ) );
    }

    // THE ZENITH IS NOT THE WORST CASE FOR A HIGH LAYER, and asserting that it was is why this guard
    // reported 10.4 samples for the shipped Cirrus sheet whose true minimum is 4.1.
    //
    // The old argument ran: looking up, the chord through the layer is exactly its thickness, and every
    // other direction gives a longer chord and therefore more samples. The first clause is true. The
    // second silently assumes the stride is a constant, and it is not — CloudStepLengthAt grows it with
    // DISTANCE, and looking down raises the distance faster than it raises the chord as soon as the ray
    // reaches the schedule's sqrt-to-linear blend band (kCloudStepFineRange 10 km to kCloudStepFarRange
    // 25 km), where the effective growth is super-linear in t.
    //
    // Measured on the two shipped geometries at the High tier. The 1.2 km sheet at 8 km is met at 8.6 km
    // looking up, still inside the sqrt regime, where the stride is 29 m; at 20 degrees of elevation it is
    // met at 25 km, deep in the blend, where the stride is 216 m. The stride grew 7.5x while the chord
    // grew 2.9x, so the samples fell from 10.4 to 4.1. The 3.5 km deck at 1.5 km never reaches the blend
    // band until 6 degrees, by which point its chord has already grown tenfold, so its minimum is 31.3 and
    // sits within 16% of its zenith value. A LOW layer really does have its worst case overhead; a high
    // one has it in the middle of the sky, which is exactly where a ground camera looks.
    //
    // So sweep the elevation and take the minimum, and report where it is — an artist cannot act on a
    // number without knowing which part of their sky it describes.
    //
    // FLAT CHORD, and it is sound here. The chord is taken as thickness / sin(elevation) and the distance
    // as (bottom + half thickness) / sin(elevation), which ignores the planet's curvature. At the 20
    // degrees where the minimum falls, curvature moves the distance to an 8 km layer by 0.6%; the sweep
    // therefore stops at 5 degrees, below which the approximation starts to overstate the distance (by 8%
    // at 5 degrees) and, with it, the stride. Overstating the stride understates the samples, so the
    // truncation is in the safe direction, and the minimum is interior and nowhere near it.
    struct CloudSearchAcrossLayer
    {
        float Samples;          // the fewest search samples any elevation gets across the layer
        float ElevationDegrees; // and where
    };

    inline CloudSearchAcrossLayer CloudWorstSearchAcrossLayer( float layerBottomAltitude, float layerThickness,
                                                               float minStep, float maxStep, float growth,
                                                               float coarseMultiplier )
    {
        // One degree of granularity over 5..90. This runs once per layer when the weather map is baked,
        // never per frame, so the sweep is free; one degree is finer than the minimum's own curvature.
        constexpr int kFirstDegree = 5;
        constexpr int kLastDegree  = 90;

        const float thickness = std::max( layerThickness, 0.0f );
        const float midpoint  = std::max( layerBottomAltitude, 0.0f ) + 0.5f * thickness;
        const float coarse    = std::max( coarseMultiplier, 1.0f );

        CloudSearchAcrossLayer worst{ std::numeric_limits<float>::max(), 90.0f };
        for ( int degree = kFirstDegree; degree <= kLastDegree; ++degree )
        {
            const float sine    = std::sin( static_cast<float>( degree ) * 3.14159265358979323846f / 180.0f );
            const float stride  = CloudStepLengthAt( midpoint / sine, minStep, maxStep, growth ) * coarse;
            const float samples = ( thickness / sine ) / std::max( stride, 1.0f );
            if ( samples < worst.Samples )
                worst = CloudSearchAcrossLayer{ samples, static_cast<float>( degree ) };
        }
        return worst;
    }

    // Four, and the number is the Nyquist argument rather than a preference: the search has to be able to
    // land inside the layer whatever its dither phase, and a phase-independent hit needs the stride to be
    // at most half the chord. Four is that bound with one doubling of margin.
    inline constexpr float kCloudMinSearchSamplesAcrossLayer = 4.0f;

    inline bool CloudCoarseStrideIsPlausible( float layerBottomAltitude, float layerThickness, float minStep,
                                              float maxStep, float growth, float coarseMultiplier )
    {
        return CloudWorstSearchAcrossLayer( layerBottomAltitude, layerThickness, minStep, maxStep, growth,
                                            coarseMultiplier )
                    .Samples >= kCloudMinSearchSamplesAcrossLayer;
    }
} // namespace Desert::Graphic
