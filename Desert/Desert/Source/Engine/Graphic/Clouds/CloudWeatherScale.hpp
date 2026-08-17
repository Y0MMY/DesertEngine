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

    // Cells a ground observer should see across the sky above that elevation. Three is the smallest
    // count that can show cloud, gap and cloud overhead.
    inline constexpr float kCloudWeatherCellsOverhead = 3.0f;

    // The tile that gives a ground observer a believable sky over a layer of this geometry, in world
    // units. Both arguments are world units (centimetres).
    inline constexpr float CloudAutoWeatherTileSize( float layerBottomAltitude, float layerThickness )
    {
        const float mid = std::max( layerBottomAltitude + 0.5f * layerThickness, 1.0f );
        return mid * ( kCloudWeatherBasePeriod * kCloudWeatherOverheadCot / kCloudWeatherCellsOverhead );
    }

    // How far an authored tile may sit from the derived one before the sky stops working. Measured on
    // Clouds_UEShowcase, whose layer wants 23.8 km: at 15 km (0.63x) the horizon degenerates into a wall
    // of cells the step schedule samples four times each, and at 60 km (2.5x) the zenith is empty. The
    // band below is the part of that range where both ends of the sky hold up, and it is what the preset
    // suite asserts and what the renderer warns outside of.
    //
    // THE BAND IS WHERE THE SLACK LIVES, and CloudLayerAspect.hpp is what took it up. A layer whose
    // thickness is fixed by its species aspect and whose tile is fixed by this relation is
    // over-determined: solving both exactly leaves one thickness per base altitude, and for the shipped
    // fair-weather bases that thickness falls under CloudMarchScale.hpp's four-sample search bound at the
    // Low tier. Of the three constraints this is the only one WITH a measured tolerance — the aspect is a
    // look measured on frames and the search bound is Nyquist — so it is the one that gives. The shipped
    // presets now sit at 1.09x to 1.41x the derived tile, well inside the band that was measured to hold
    // up at both ends of the sky.
    inline constexpr float kCloudWeatherTileToleranceLow  = 0.7f;
    inline constexpr float kCloudWeatherTileToleranceHigh = 1.6f;

    inline constexpr bool CloudWeatherTileIsPlausible( float weatherTileSize, float layerBottomAltitude,
                                                       float layerThickness )
    {
        const float wanted = CloudAutoWeatherTileSize( layerBottomAltitude, layerThickness );
        return weatherTileSize >= wanted * kCloudWeatherTileToleranceLow &&
               weatherTileSize <= wanted * kCloudWeatherTileToleranceHigh;
    }
} // namespace Desert::Graphic
