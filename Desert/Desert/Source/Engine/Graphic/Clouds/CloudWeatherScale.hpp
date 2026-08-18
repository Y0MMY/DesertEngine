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
    // a flat top — and the only thing that has ever made one read as a cloud is the detail erosion. That
    // is why raising this constant alone is not the fix, and why CloudIslandScale.hpp exists.
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
    // ground-truth probe renders the same wall, and the mechanism is that a cell that small carries a
    // coverage island smaller than the erosion the march can still carry at that distance, so the island
    // arrives unsculpted — a box. See CloudIslandScale.hpp, which is the relation that says so, and
    // CloudGeometry.glslh's CLOUD_WEATHER_CELLS_OVERHEAD note for the knock-out table behind it.
    //
    // THE BAND IS WHERE THE SLACK LIVES, and CloudLayerAspect.hpp is what took it up. A layer whose
    // thickness is fixed by its species aspect and whose tile is fixed by this relation is
    // over-determined: solving both exactly leaves one thickness per base altitude, and for the shipped
    // fair-weather bases that thickness falls under CloudMarchScale.hpp's four-sample search bound at the
    // Low tier. Of the three constraints this is the only one WITH a measured tolerance — the aspect is a
    // look measured on frames and the search bound is Nyquist — so it is the one that gives. WHERE THE
    // SHIPPED PRESETS SIT against the derived tile, since the deck-scale decision: the four cumulus rows
    // (Clear, Fair Weather, Partly Cloudy, Summer Cumulus) are re-derived and sit at 1.000; Stratus, Storm
    // and Cirrus keep authored geometry that was exactly the derived tile at three cells overhead, so
    // raising the constant to four puts all three at 1.333; Overcast keeps an authored tile that was
    // 1.2465x its own derived one and therefore lands at 1.662 — outside the band, and exempt for the
    // reason the clause below states rather than by name.
    inline constexpr float kCloudWeatherTileToleranceLow  = 0.7f;
    inline constexpr float kCloudWeatherTileToleranceHigh = 1.6f;

    // ---- The applicability clause, shared with CloudIslandScale.hpp -------------------------------------
    //
    // The coverage at and above which the weather field is CONNECTED: one blanket rather than a field of
    // discrete cells with gaps between them. 0.90 is where the shipped Stratus row sits and the comparison
    // is inclusive, so that row is exempt by its physics and not by a rounding accident.
    //
    // ONE THRESHOLD, TWO RELATIONS, STATED ONCE. It lives here, in the header that owns the weather field,
    // and CloudIslandScale.hpp reads it from here. A second literal 0.90 in the other file would be two
    // spellings of one number, which is the drift this project keeps paying for.
    inline constexpr float kCloudConnectedCoverage = 0.90f;

    inline constexpr bool CloudCoverageFieldIsConnected( float coverage )
    {
        return coverage >= kCloudConnectedCoverage;
    }

    // WHY THIS PREDICATE TAKES THE COVERAGE. The quantity it bounds is "how many discrete coverage cells
    // does a ground observer see across the sky above 20 degrees", and on a SHEET that is not an
    // observable: above kCloudConnectedCoverage there are no discrete cells to count. Both ends of the
    // measured band are failures of a BROKEN sky and neither exists on a blanket — 2.5x empties the zenith
    // because the one cell overhead is as likely to be a hole as a cloud, and a connected field has no
    // holes; 0.63x walls the horizon because the islands out there fall below the erosion the march can
    // carry (CloudIslandScale.hpp), and a connected field has no islands. So the relation is vacuous on a
    // sheet, and it says so in code rather than in a note beside itself — or in a by-name exemption in a
    // test, which is content baked into a mechanism.
    //
    // WHAT THIS DOES NOT CLAIM, and the next reader needs it in the same breath: a sheet's Weather Tile
    // Size is NOT a free parameter. It still sets the horizontal scale of the blanket's own undulations —
    // the thickening and thinning that makes an overcast sky read as weather rather than as fog — and a
    // tile of 500 m or of 500 km is as wrong there as anywhere. What is vacuous is the CELLS-OVERHEAD
    // question specifically, because it counts something a connected field does not have. Nothing in this
    // engine currently bounds a blanket's undulation scale; that is an absence, recorded here, not a
    // licence.
    inline constexpr bool CloudWeatherTileIsPlausible( float coverage, float weatherTileSize,
                                                       float layerBottomAltitude, float layerThickness )
    {
        if ( CloudCoverageFieldIsConnected( coverage ) )
            return true;

        const float wanted = CloudAutoWeatherTileSize( layerBottomAltitude, layerThickness );
        return weatherTileSize >= wanted * kCloudWeatherTileToleranceLow &&
               weatherTileSize <= wanted * kCloudWeatherTileToleranceHigh;
    }
} // namespace Desert::Graphic
