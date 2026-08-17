#pragma once

#include <Common/Core/Units.hpp>

#include <Engine/Graphic/Clouds/CloudWeatherScale.hpp>

#include <algorithm>

// THE THIRD SCALE RELATION: a cloud is WIDER THAN IT IS TALL, and how much wider is the species.
//
// The two relations that already existed each tie the layer to one other quantity — CloudWeatherScale.hpp
// ties the weather tile to the layer's altitude, CloudMarchScale.hpp ties the march's stride to the layer's
// thickness. Neither says anything about the layer's own PROPORTIONS, and that was the hole: every
// fair-weather scene in the repository was authored with a 3.5 km deck under a 2.98 km coverage cell, i.e.
// a cloud a fifth taller than it was wide. Those are cumulonimbus proportions on a fair-weather sky, and
// they are why a cloud overhead loomed like a ceiling instead of sitting up in the atmosphere. The
// individual numbers were each defensible; it is the RATIO between them that nothing expressed.
//
// WHAT THE RATIO IS BETWEEN. The weather map's FBM starts at CLOUD_WEATHER_BASE_PERIOD cells across one
// tile (Programs/Clouds/CloudWeather.shader), so the DOMINANT coverage cell — one cloud, or one cluster of
// them — is `WeatherTileSize / 8` wide. The layer's thickness is how tall that cloud can grow. Their ratio
// is therefore the aspect of the cloud the sky is actually made of, and it is a directly observable
// quantity: it is what decides whether the eye reads a cumulus field or a ceiling.
//
// MEASURED, not asserted. On Clouds_PartlyCloudy (base 1.5 km, cell 2.98 km) the layer was swept at fixed
// weather scale and looked at from three elevations: 3.5 km (aspect 0.85) is the shipped ceiling; 0.9 km
// (aspect 3.31) gives a correct receding plane at the horizon but pancakes at mid elevation; 1.6 km
// (aspect 1.86) reads right at all three — cumulus with real vertical development, clearly wider than
// tall, flat bases, sitting at altitude. That measurement is what the cumulus targets below are anchored
// to, and it is why they are numbers rather than a range.
//
// NO GLSL MIRROR, deliberately. CloudWeatherScale and CloudMarchScale are both mirrors because the shader
// consumes the same schedule the CPU decides from. This relation is consumed by NOBODY on the GPU: the
// raymarch reads the tile and the thickness and never needs their ratio. A mirror would be a second copy
// with no second reader, which is the defect this project has paid for rather than a guard against it.
// The relation is therefore stated once, here, in C++, and pinned by the CloudPresets suite.

namespace Desert::Graphic
{
    // The realised aspect of the layer's dominant cloud: how many times wider than tall. Both arguments are
    // world units (centimetres); the result is a pure ratio.
    inline constexpr float CloudLayerAspect( float weatherTileSize, float layerThickness )
    {
        return ( weatherTileSize / kCloudWeatherBasePeriod ) / std::max( layerThickness, 1.0f );
    }

    // The inverse, and the derived default: the thickness at which a layer under this weather tile realises
    // @p aspect. This is what the preset table's LayerThickness values ARE — the CloudPresets suite asserts
    // each row against its own documented species target, so a hand-edited thickness fails a test rather
    // than quietly reintroducing a ceiling.
    inline constexpr float CloudLayerThicknessForAspect( float weatherTileSize, float aspect )
    {
        return ( weatherTileSize / kCloudWeatherBasePeriod ) / std::max( aspect, 1e-3f );
    }

    // ---- Per-species targets ---------------------------------------------------------------------------
    //
    // Aspect is a property of the SPECIES, in the sense the cloud atlas uses the word, and the order below
    // is the convective one the component's Cloud Type slider already walks: the deeper the convection, the
    // narrower the cloud relative to its own height. It stops being a function of Cloud Type at the two
    // ends — a low stratus sheet and a cirrus one differ by étage rather than by form and land at opposite
    // aspects — so the engine does not try to infer the species. Each PRESET names its species; this is the
    // vocabulary it names it from.
    //
    // A SPECIES IS A RANGE, NOT A NUMBER, and that is not vagueness: real cumulus humilis are 1.5 to 3
    // times wider than deep depending on how vigorous the day is, and a renderer that pinned one value per
    // species would be inventing precision the atmosphere does not have. So the vocabulary gives the range,
    // each preset picks one number inside it, and the CloudPresets suite asserts both — the row's realised
    // aspect against its own target, and the target against its species' range. Where a preset sits in its
    // range is decided by the ceiling note at the bottom of this file.
    struct CloudSpeciesAspect
    {
        const char* Name;
        float       Low;
        float       High;
    };

    // THE MEASURED ANCHOR. Everything else is placed relative to this one: the sweep described above put a
    // fair-weather cumulus deck's readable aspect at 1.85, and that number came off frames rather than off a
    // table. It sits inside the mediocris range below and is what Partly Cloudy — the default look, and the
    // scene the sweep was run on — is authored to.
    inline constexpr float kCloudAspectMeasuredCumulus = 1.85f;

    inline constexpr CloudSpeciesAspect kCloudSpeciesCumulusHumilis{ "Cumulus humilis", 1.5f, 3.0f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesCumulusMediocris{ "Cumulus mediocris", 1.2f, 2.5f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesCumulusCongestus{ "Cumulus congestus", 0.8f, 1.5f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesStratocumulus{ "Stratocumulus", 1.2f, 4.0f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesStratus{ "Stratus", 1.0f, 6.0f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesCumulonimbus{ "Cumulonimbus", 0.4f, 0.9f };
    inline constexpr CloudSpeciesAspect kCloudSpeciesCirrus{ "Cirrus", 4.0f, 12.0f };

    inline constexpr bool CloudAspectSuitsSpecies( float aspect, const CloudSpeciesAspect& species )
    {
        return aspect >= species.Low && aspect <= species.High;
    }

    // ---- The bound the renderer warns on ---------------------------------------------------------------
    //
    // The targets above are INTENT: a preset that misses its species by a little is a look note, not a
    // defect. What follows is the part that is not a matter of taste, and it is the one the renderer states
    // out loud, in the same spirit as CloudWeatherTileIsPlausible and CloudCoarseStrideIsPlausible.
    //
    // A cloud narrower than it is tall is a CONVECTIVE TOWER, and a convective tower is deep. Cumulonimbus
    // reach the upper troposphere — 8-14 km deep in mid-latitudes, and the species is not recognised below
    // roughly six. So a layer may be taller than it is wide only if it is deep enough to be one. Below that
    // depth the same proportions describe nothing in the sky: they describe a slab standing on end, which is
    // exactly what the shipped fair-weather family was and exactly what the owner saw as a ceiling.
    //
    // The threshold is on the LAYER, not on any individual cloud, because the layer is what the scene
    // authors and what the march measures.
    inline constexpr float kCloudDeepConvectionThickness = Common::Units::Metres( 6000.0f );

    // Below that depth, a layer must be at least as wide as it is tall. One, and not the species target,
    // because this is the impossibility bound and not the intent: FairWeather shipped at 1.007 and is wrong
    // for its species without being physically absurd, and a warning that fired on it would be a warning
    // about taste.
    inline constexpr float kCloudMinAspectBelowDeepConvection = 1.0f;

    inline constexpr bool CloudLayerAspectIsPlausible( float weatherTileSize, float layerThickness )
    {
        return layerThickness >= kCloudDeepConvectionThickness ||
               CloudLayerAspect( weatherTileSize, layerThickness ) >= kCloudMinAspectBelowDeepConvection;
    }

    // ---- What caps the targets, measured -------------------------------------------------------------
    //
    // Where each preset sits inside its species' range is not taste: it is the widest the RENDERER can
    // carry, and the limit is CloudMarchScale.hpp's search bound at the Low quality tier. Low strides
    // 4 x a 60 m minimum step, which overhead is about 350 m, so a layer thinner than ~1.4 km falls under
    // the four samples the empty-space search needs and becomes a per-pixel coin toss. Against the shipped
    // coverage cells that is an aspect ceiling of 1.88 for Clear, 1.78 for Fair Weather, 2.11 for Partly
    // Cloudy, 1.50 for Summer Cumulus and 1.37 for Overcast. Every preset target sits under its own
    // ceiling with margin — which is why Fair Weather is at 1.70, in the lower half of the mediocris
    // range rather than the upper, and why Overcast is a 1.30 blanket near the bottom of the
    // stratocumulus range rather than the broad sheet the species can also be. Lifting either means
    // lowering Low's Coarse Step Multiplier, which is a performance trade and belongs to the quality
    // tiers rather than to the layer.
} // namespace Desert::Graphic
