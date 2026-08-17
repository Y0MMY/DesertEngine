#pragma once

#include <algorithm>
#include <cmath>

// The relation between a layer's THICKNESS and the stride the empty-space search takes through it —
// the second scale relation this renderer needs and the one a THIN layer walks straight into.
//
// WHAT IT IS FOR. The march skips empty sky at CoarseStepMultiplier times the fine stride, and only slows
// down once a sample comes back non-empty. A layer whose own chord is comparable with that stride can be
// STEPPED OVER: whether a given ray notices the cloud at all becomes a function of that ray's dither
// phase, i.e. a per-pixel coin toss. The temporal stage cannot rescue it, because the average of "half
// the rays missed the cloud" is "half the cloud" — it converges to a stipple rather than removing one.
//
// It was found on the first two-layer frame ever rendered: a 1.2 km cirrus sheet authored with the same
// CoarseStepMultiplier of 4-5 the 3.5 km deck uses drew a regular cross-hatch lattice across every wisp.
// Dropping the sheet's coarse stride below its own thickness removed it completely, with no other change.
// The deck never showed it because 3.5 km is forty coarse strides, not three.
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

    // How many SEARCH samples a ray takes across the layer's own thickness, at the nearest distance the
    // layer can be met from the ground — its base altitude, straight up. That is the WORST case for this
    // relation and therefore the one to test: looking up, the chord through the layer is exactly its
    // thickness, and every other direction gives the ray a longer chord and more samples.
    inline float CloudSearchSamplesAcrossLayer( float layerBottomAltitude, float layerThickness, float minStep,
                                                float maxStep, float growth, float coarseMultiplier )
    {
        const float stride = CloudStepLengthAt( std::max( layerBottomAltitude, 0.0f ), minStep, maxStep, growth ) *
                             std::max( coarseMultiplier, 1.0f );
        return std::max( layerThickness, 0.0f ) / std::max( stride, 1.0f );
    }

    // Four, and the number is the Nyquist argument rather than a preference: the search has to be able to
    // land inside the layer whatever its dither phase, and a phase-independent hit needs the stride to be
    // at most half the chord. Four is that bound with one doubling of margin, which is where the frames
    // stop showing the lattice — at three it is faint and at two it is the cross-hatch that found this.
    inline constexpr float kCloudMinSearchSamplesAcrossLayer = 4.0f;

    inline bool CloudCoarseStrideIsPlausible( float layerBottomAltitude, float layerThickness, float minStep,
                                              float maxStep, float growth, float coarseMultiplier )
    {
        return CloudSearchSamplesAcrossLayer( layerBottomAltitude, layerThickness, minStep, maxStep, growth,
                                              coarseMultiplier ) >= kCloudMinSearchSamplesAcrossLayer;
    }
} // namespace Desert::Graphic
