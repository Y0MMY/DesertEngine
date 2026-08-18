#pragma once

#include <Common/Core/Units.hpp>

#include <algorithm>

// The relation between the WEATHER field's horizontal scale and the layer's altitude — the one thing
// nothing in this renderer expressed, and the reason Clouds_UEShowcase rendered a dense band at the
// horizon and empty blue above twenty degrees.
//
// The reasoning, the constants and the shader-side statement of the same formula live in
// Editor/Resources/Shaders/Common/CloudGeometry.glslh (CloudAutoWeatherTileSize). This is its C++
// mirror, for the places that decide the number rather than consume it: the component's default, the
// preset table, and the warning below. A mirror of a formula is exactly the shape of defect this project
// has paid for before (RESEARCH_REFERENCE: "a C++ mirror of a gradient vs the shader's own formula"), so
// the CloudMath suite compiles the .glslh as C++ beside this header and asserts the two agree to the
// bit. Change one and that test fails.

namespace Desert::Graphic
{
    // Coverage cells across one weather tile — Programs/Clouds/CloudWeather.shader's FBM base period.
    inline constexpr float kCloudWeatherBasePeriod = 8.0f;

    // 1 / tan(20 degrees): the elevation above which the showcase's sky measurably emptied.
    inline constexpr float kCloudWeatherOverheadCot = 2.7474774f;

    // Cells a ground observer should see across the sky above that elevation.
    //
    // FOUR, AND IT USED TO BE THREE. Three is the smallest count that can show cloud, gap and cloud
    // overhead — a MINIMUM, adopted as the TARGET, and that substitution is the whole reason every sky in
    // this repository read as a ceiling. Measured across the eight shipped presets the dominant coverage
    // cell sat at 0.92 to 1.29 times the layer's own mid altitude, so a cloud overhead subtended 49 to 66
    // degrees and two clouds covered the sky from horizon to horizon. At four, the worst row (Partly
    // Cloudy) goes from 65.7 to 37.9 degrees overhead. Docs/Clouds/DECK_SCALE_DECISION.md carries the
    // per-preset table and the decision.
    //
    // THE REASON THIS WAS NOT RAISED SOONER WAS A WRONG DIAGNOSIS, and it is corrected in full where the
    // formula lives (CloudGeometry.glslh, the CLOUD_WEATHER_CELLS_OVERHEAD note). In one line: the
    // 30-degree trial's wall at the horizon was blamed on the step schedule, and a converged ground-truth
    // probe with no state machine, no dither and cheap density only renders the same boxes. The boxes are
    // a property of the DENSITY FIELD — a 2-D coverage field extruded vertically between a flat base and
    // a flat top — and the only thing that has ever made one read as a cloud is the detail erosion. So
    // raising this constant is not on its own a complete answer to the boxes; what it fixes is the
    // angular size of a cloud overhead, which is what the owner reported.
    inline constexpr float kCloudWeatherCellsOverhead = 4.0f;

    // The tile that gives a ground observer a believable sky over a layer of this geometry, in world
    // units. Both arguments are world units (centimetres).
    inline constexpr float CloudAutoWeatherTileSize( float layerBottomAltitude, float layerThickness )
    {
        const float mid = std::max( layerBottomAltitude + 0.5f * layerThickness, 1.0f );
        return mid * ( kCloudWeatherBasePeriod * kCloudWeatherOverheadCot / kCloudWeatherCellsOverhead );
    }

    // How far an authored tile may sit from the derived one before the sky stops working. Measured on
    // Clouds_UEShowcase: at 0.63x the derived tile the horizon degenerates into a wall of cells, and at
    // 2.5x the zenith is empty. The band below is the part of that range where both ends of the sky hold
    // up, and it is what the preset suite asserts and what the renderer warns outside of.
    //
    // THE MEASUREMENT SURVIVED A RE-DIAGNOSIS; THE EXPLANATION THAT USED TO SIT HERE DID NOT. The wall at
    // 0.63x was recorded as "cells the step schedule samples four times each". It is not: a converged
    // ground-truth probe with 768 uniform samples, no state machine and no dither renders the same wall.
    // What the mechanism actually is has not been established with numbers, so this header does not claim
    // one. See CloudGeometry.glslh's CLOUD_WEATHER_CELLS_OVERHEAD note for the knock-out table.
    //
    // THE BAND IS WHERE THE SLACK LIVES, and CloudLayerAspect.hpp is what took it up. A layer whose
    // thickness is fixed by its species aspect and whose tile is fixed by this relation is
    // over-determined: solving both exactly leaves one thickness per base altitude, and for the shipped
    // fair-weather bases that thickness falls under CloudMarchScale.hpp's four-sample search bound at the
    // Low tier. Of the three constraints this is the only one WITH a measured tolerance — the aspect is a
    // look measured on frames and the search bound is Nyquist — so it is the one that gives.
    //
    // ---- WHY THE BAND IS DERIVED AND NOT WRITTEN DOWN --------------------------------------------------
    //
    // THE BAND IS A RATIO TO THE DERIVED TILE, AND THE DERIVED TILE MOVES WITH kCloudWeatherCellsOverhead.
    // Its two endpoints, however, were measured as ABSOLUTE tiles on ONE layer — Clouds_UEShowcase, whose
    // derived tile was 23.8 km at the three cells overhead the constant carried then. So raising the count
    // to four rescaled the derived tile by 0.75x while two hardcoded ratios stayed put, and the band
    // silently moved by 4/3 relative to the frames that define it. That is not a tuning question: it made
    // an authored row leave a band it had not moved relative to.
    //
    // So the ratios are DERIVED from the count, and the two literals below are historical facts that never
    // move again: the band as measured, and the count it was measured under. Change
    // kCloudWeatherCellsOverhead and the band follows it exactly, which is the only way it stays the same
    // band. The absolute failures behind the measurement, for the record: on that layer 15 km walled the
    // horizon (0.63x its 23.8 km) and 60 km emptied the zenith (2.5x); the shipped band sits inside both
    // with margin, and it is the band and not the failures that is the shipped guarantee.
    inline constexpr float kCloudWeatherTileToleranceLowAsMeasured  = 0.7f;
    inline constexpr float kCloudWeatherTileToleranceHighAsMeasured = 1.6f;
    inline constexpr float kCloudWeatherCellsOverheadAtMeasurement  = 3.0f;

    inline constexpr float kCloudWeatherTileBandRescale =
         kCloudWeatherCellsOverhead / kCloudWeatherCellsOverheadAtMeasurement;

    inline constexpr float kCloudWeatherTileToleranceLow =
         kCloudWeatherTileToleranceLowAsMeasured * kCloudWeatherTileBandRescale;
    inline constexpr float kCloudWeatherTileToleranceHigh =
         kCloudWeatherTileToleranceHighAsMeasured * kCloudWeatherTileBandRescale;

    // ---- A KNOWN, QUANTIFIED DEFECT: THE DERIVED ROWS SIT 7% ABOVE THE MEASURED WALL -------------------
    //
    // This is the most important thing to know about this relation and it is not a passing remark.
    //
    // Under the corrected band a row solved EXACTLY to CloudAutoWeatherTileSize sits at 1.000, and the
    // band's low end is 0.9333. So the four cumulus presets — Clear, Fair Weather, Partly Cloudy, Summer
    // Cumulus, every one of them derived rather than authored — sit **7.1% above the tile at which the
    // horizon was measured to wall**. The derivation does not aim at the middle of the measured range; it
    // lands just inside its bad end.
    //
    // THAT IS VISIBLE TODAY. It is the band of hard-edged boxes above the horizon on the cumulus scenes:
    // the same failure the 0.9333 endpoint records, one twelfth of the way back from it. A relation whose
    // solution sits 7% from a measured failure is not a passing guard, it is a guard the shipped content
    // is standing on the edge of, and the next change to either side of it — the count, the aspect, the
    // schedule — can push it over with no test going red, because 1.000 is inside 0.9333 and always will
    // be. Where the other rows sit: Stratus, Storm and Cirrus at 1.333 (they were the derived tile at
    // three cells), Overcast at 1.662 (an authored tile at 1.2465x its own derived one).
    //
    // WHAT WOULD ANSWER IT, so this is a finding and not a shrug: the mechanism behind the wall at 0.63x
    // is not established — the step-schedule explanation was disproved by a converged ground-truth probe
    // and nothing replaced it. Until it is, "move the derivation up inside the band" is a number chosen to
    // look better rather than a fix, and this header will not do that.

    // The quantity this predicate bounds is "how many discrete coverage cells does a ground observer see
    // across the sky above 20 degrees". It takes the geometry and nothing else: the tile and the altitude
    // are the two sides of the relation, and a third input would be a third opinion about them.
    inline constexpr bool CloudWeatherTileIsPlausible( float weatherTileSize, float layerBottomAltitude,
                                                       float layerThickness )
    {
        const float wanted = CloudAutoWeatherTileSize( layerBottomAltitude, layerThickness );
        return weatherTileSize >= wanted * kCloudWeatherTileToleranceLow &&
               weatherTileSize <= wanted * kCloudWeatherTileToleranceHigh;
    }
} // namespace Desert::Graphic
