#pragma once

#include <cstdint>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <Common/Core/Units.hpp>

#include <Engine/Reflection/ReflectionMacros.hpp>

namespace Desert::ECS
{
    // Quality tier. Anything but Custom overwrites the thirteen knobs below it in the Quality group, so a
    // tier is a shorthand for a full set of raymarch settings rather than a separate multiplier.
    enum class CloudQuality : uint8_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Custom // the thirteen knobs are authored by hand
    };

    // Resolution the raymarch runs at, relative to the render target. Half is the usual choice: the cloud
    // signal is low-frequency, the temporal stage puts the detail back.
    enum class CloudResolutionScale : uint8_t
    {
        Quarter,
        Half,
        Full
    };

    enum class CloudTemporalMode : uint8_t
    {
        Off,         // every frame is marched in full - the reference the temporal path is compared against
        Reprojection // accumulate over frames, reprojecting by camera motion
    };

    // Which weather preset the look last came from. An enum, not a string, for the same reason as
    // SkyPreset: a string can name a preset that does not exist and nothing between the scene file and the
    // widget would notice.
    enum class CloudPreset : uint8_t
    {
        Custom, // not a preset: "these values were authored by hand"
        Clear,
        FairWeather,
        PartlyCloudy,
        SummerCumulus,
        Stratus,
        Overcast,
        Storm,
        Cirrus
    };

    // Volumetric clouds: a raymarched shell around the planet, replacing the flat noise layer that used to
    // be painted in the sky shader. The defaults below are the Partly Cloudy preset.
    //
    // Every distance is in world units, i.e. CENTIMETRES, written through Common::Units::Metres so the
    // altitude an artist would quote in metres stays readable in the source.
    //
    // The component carries MULTIPLIERS AND TINTS for lighting, never absolute colours: how bright the sun
    // is and what colour the sky is come from the atmosphere (SkyAtmosphereComponent), so a cloud cannot
    // disagree with the sky it hangs in.
    struct VolumetricCloudData
    {
        REFLECT()

        // ---- Cloud Layer ------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Enabled" ), Category( "Cloud Layer" ), Summary,
                  Tooltip( "Master switch for the volumetric cloud layer. Clouds are not baked into the "
                           "sky's environment lighting, so an overcast sky does not darken the scene." ) )
        bool Enabled = true;

        PROPERTY( DisplayName( "Layer Bottom Altitude" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, Common::Units::Metres( 20000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Cloud base height above the planet surface." ) )
        float LayerBottomAltitude = Common::Units::Metres( 1500.0f );

        PROPERTY( DisplayName( "Layer Thickness" ), Category( "Cloud Layer" ), Length,
                  Range( Common::Units::Metres( 50.0f ), Common::Units::Metres( 15000.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Vertical extent of the shell. Tall layers give towering cumulus." ) )
        float LayerThickness = Common::Units::Metres( 3500.0f );

        PROPERTY( DisplayName( "Max View Distance" ), Category( "Cloud Layer" ), Length,
                  Range( Common::Units::Metres( 5000.0f ), Common::Units::Metres( 400000.0f ) ),
                  EditCondition( "Enabled" ), Tooltip( "How far along the view ray clouds are marched at all." ) )
        float MaxViewDistance = Common::Units::Metres( 150000.0f );

        PROPERTY( DisplayName( "Horizon Fade Start" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, Common::Units::Metres( 400000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which clouds start dissolving into the sky." ) )
        float HorizonFadeStart = Common::Units::Metres( 60000.0f );

        PROPERTY( DisplayName( "Horizon Fade End" ), Category( "Cloud Layer" ), Length,
                  Range( 0.0f, Common::Units::Metres( 400000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which clouds are fully gone." ) )
        float HorizonFadeEnd = Common::Units::Metres( 140000.0f );

        // ---- Weather ----------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Coverage" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Fraction of the sky filled. The single biggest look knob." ) )
        float Coverage = 0.80f;

        PROPERTY( DisplayName( "Coverage Contrast" ), Category( "Weather" ), Range( 0.2f, 4.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "High values give hard-edged islands, low values a soft blanket." ) )
        float CoverageContrast = 1.20f;

        // 23 811.5 m is not a taste: it is CloudAutoWeatherTileSize (Common/CloudGeometry.glslh) for the
        // default 1.5-5 km layer above. That function is the relation between this tile and the layer's
        // altitude — how many coverage cells a ground observer gets to see overhead — and every preset
        // and every scene in the repository is now authored from it. Moving the layer without moving
        // this is what emptied Clouds_UEShowcase's zenith; the CloudPresets suite pins the pair.
        PROPERTY( DisplayName( "Weather Tile Size" ), Category( "Weather" ), Length,
                  Range( Common::Units::Metres( 5000.0f ), Common::Units::Metres( 400000.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "World size of one weather-map tile, i.e. the size of a cloud SYSTEM. Roughly "
                           "7.3x the layer's mid-altitude gives a ground observer a filled sky." ) )
        float WeatherTileSize = Common::Units::Metres( 23811.5f );

        PROPERTY( DisplayName( "Weather Seed" ), Category( "Weather" ), Range( 0, 65535 ),
                  EditCondition( "Enabled" ), Tooltip( "Reshuffles the whole cloudscape layout." ) )
        int WeatherSeed = 1337;

        PROPERTY( DisplayName( "Weather Octaves" ), Category( "Weather" ), Range( 1, 8 ),
                  EditCondition( "Enabled" ), Tooltip( "Detail richness of the coverage field." ) )
        int WeatherOctaves = 6;

        PROPERTY( DisplayName( "Weather Warp Strength" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Domain warp of the weather map: turns circular blobs into organic fronts." ) )
        float WeatherWarpStrength = 0.45f;

        PROPERTY( DisplayName( "Cloud Type" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Which authored vertical FORM the clouds take, walked in order of convective "
                           "development: 0 = flat scud/stratus, 0.2 = a hard-based stratocumulus shelf, "
                           "0.4 = stratocumulus, 0.6 = cumulus, 0.8 = cauliflower congestus, "
                           "1 = a cumulonimbus anvil with a pinched stalk." ) )
        float CloudType = 0.60f;

        PROPERTY( DisplayName( "Cloud Type Variance" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "How far the type wanders from cloud cluster to cloud cluster. 0 gives one "
                           "form over the whole sky; high values put shelves, towers and anvils in the "
                           "same frame." ) )
        float CloudTypeVariance = 0.45f;

        PROPERTY( DisplayName( "Cloud Height Variance" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "How much neighbouring clouds differ in DEPTH. 0 makes every cloud fill the "
                           "whole layer, as the layer's single global slab always did; raising it gives "
                           "each cell its own vertical slab. Tops move about five times as far as bases "
                           "do - a cloud field condenses at one level and climbs to a hundred different "
                           "ones, and it is the flat bases all at one altitude that make a deck read as a "
                           "ceiling instead of as fog." ) )
        float CloudHeightVariance = 0.55f;

        PROPERTY( DisplayName( "Anvil Bias" ), Category( "Weather" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Spreads cloud tops outward - the cumulonimbus anvil." ) )
        float AnvilBias = 0.10f;

        PROPERTY( DisplayName( "Wetness" ), Category( "Weather" ), Range( 0.0f, 1.0f ), EditCondition( "Enabled" ),
                  Tooltip( "Rain-laden look: darker, denser bases." ) )
        float Wetness = 0.15f;

        // ---- Shape ------------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Shape Tile Size" ), Category( "Shape" ), Length,
                  Range( Common::Units::Metres( 2000.0f ), Common::Units::Metres( 200000.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "World size of one base-noise tile, i.e. the size of an individual cloud." ) )
        float ShapeTileSize = Common::Units::Metres( 35000.0f );

        PROPERTY( DisplayName( "Shape Seed" ), Category( "Shape" ), Range( 0, 65535 ), EditCondition( "Enabled" ),
                  Tooltip( "Regenerates the shape volume. Changing it re-bakes the noise." ) )
        int ShapeSeed = 7;

        PROPERTY( DisplayName( "Base Shape Remap Min" ), Category( "Shape" ), Range( 0.0f, 0.9f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Threshold below which base noise is empty. Raising it gives sparser, crisper "
                           "clouds." ) )
        float BaseShapeRemapMin = 0.30f;

        PROPERTY( DisplayName( "Shape Erosion Strength" ), Category( "Shape" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "How much the FBM octaves eat into the base blob." ) )
        float ShapeErosionStrength = 0.65f;

        PROPERTY( DisplayName( "Extinction Scale" ), Category( "Shape" ), Range( 0.01f, 8.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Optical density. The most important single number in the system." ) )
        float ExtinctionScale = 1.00f;

        // THE BASE-IN WIDTH IS WHAT A CLOUD BOTTOM IS MADE OF, and every row below is authored from that.
        //
        // The profile is thresholded by the detail erosion, so a column becomes cloud at the height where
        //     gradient(h) x coverage x baseShape > detailThreshold.
        // Only `gradient` depends on h, so the base altitude is that equation SOLVED for h — and every
        // horizontally-varying factor on the right is converted into a vertical displacement of the base
        // by the inverse of the base-in ramp. Measured on the Partly Cloudy deck (CPU probe over one
        // weather tile, 6400 columns): the base-altitude scatter is 0.40 x the ramp's own height. A ramp
        // of 0.22 of a 3 km cell is 650 m, giving 260 m of scatter with the per-cell band held fixed;
        // there is no ceiling in that, and it is the larger half of "the clouds look close to the ground".
        //
        // So the ramps are authored as a LENGTH the atmosphere justifies rather than as a shape that
        // looked right: liquid water content reaches near-adiabatic within roughly 100 m of the lifting
        // condensation level, so a base-in of 0.05 of a ~3 km cell (150 m, 60 m of scatter) is the
        // physical figure and 0.22 was four times it.

        PROPERTY( DisplayName( "Stratus Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "Base-in start/end and top-out start/end of the stratus height profile, in "
                           "normalized layer height (0 = layer bottom, 1 = layer top). A sheet sits ON "
                           "its condensation level, so the base-in is a few hundredths wide." ) )
        glm::vec4 StratusGradient = { 0.00f, 0.05f, 0.20f, 0.32f };

        PROPERTY( DisplayName( "Stratocumulus Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "The same four heights for the stratocumulus profile. A cellular deck shares "
                           "one condensation level across the whole sky, so its base is nearly as flat as "
                           "a stratus sheet's." ) )
        glm::vec4 StratocumulusGradient = { 0.00f, 0.06f, 0.55f, 0.78f };

        PROPERTY( DisplayName( "Cumulus Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "The same four heights for the cumulus profile. Fair-weather cumulus have the "
                           "SHARPEST bases in the sky - the flat bottoms all at one altitude that say the "
                           "ceiling is up there - so this is the narrowest base-in of the convective "
                           "forms." ) )
        glm::vec4 CumulusGradient = { 0.00f, 0.05f, 0.68f, 0.92f };

        // The three forms the Cloud Type axis gained. Each is a trapezoid PLUS a bend: a trapezoid
        // product is a monotone rise times a monotone fall, so it can put a cloud's mass higher or lower
        // but can never pinch a waist into it or leave a stalk under a flaring head. The bend is what
        // makes the type knob choose a SHAPE instead of a height. See CloudProfileCurves.hpp.

        PROPERTY( DisplayName( "Shelf Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "The four heights of the stratocumulus SHELF profile. Its base-in range is "
                           "deliberately a few hundredths wide: that hard, flat, all-at-one-altitude "
                           "base is what a shelf cloud is." ) )
        glm::vec4 ShelfGradient = { 0.02f, 0.05f, 0.44f, 0.52f };

        PROPERTY( DisplayName( "Shelf Profile Form" ), Category( "Shape" ), Range( -1.0f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "Bend applied to the shelf profile: (centre, half-width, amount) in normalized "
                           "layer height. Amount 0 leaves a plain trapezoid, negative pinches a waist, "
                           "positive thickens a belly." ) )
        glm::vec3 ShelfProfileForm = { 0.28f, 0.30f, 0.35f };

        PROPERTY( DisplayName( "Congestus Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "The four heights of the cauliflower congestus profile - a tall trapezoid with "
                           "a short plateau, so the bend below can round it into a bulging mass. A "
                           "congestus shares the cumulus condensation level and spends its extra depth "
                           "UPWARD, so its base-in stays narrow however tall the tower gets." ) )
        glm::vec4 CongestusGradient = { 0.00f, 0.07f, 0.70f, 1.00f };

        PROPERTY( DisplayName( "Congestus Profile Form" ), Category( "Shape" ), Range( -1.0f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "Bend applied to the congestus profile: (centre, half-width, amount). A "
                           "positive amount at mid height is the cauliflower bulge - full through the "
                           "middle, tapering toward both the base and the top." ) )
        glm::vec3 CongestusProfileForm = { 0.62f, 0.42f, 1.20f };

        PROPERTY( DisplayName( "Anvil Gradient" ), Category( "Shape" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "The four heights of the cumulonimbus anvil profile: a box that fills nearly "
                           "the whole layer, which the bend below then carves a stalk out of. This is the "
                           "one form whose base-in is deliberately WIDE - a storm's base is lowered and "
                           "diffuse, ragged with scud and streaked with rain shafts falling out of it, "
                           "and that softness is the species rather than a defect." ) )
        glm::vec4 AnvilGradient = { 0.00f, 0.16f, 0.92f, 1.00f };

        PROPERTY( DisplayName( "Anvil Profile Form" ), Category( "Shape" ), Range( -1.0f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ),
                  Tooltip( "Bend applied to the anvil profile: (centre, half-width, amount). A strong "
                           "negative amount low down is the pinched stalk that leaves the head flaring "
                           "alone against the top of the layer." ) )
        glm::vec3 AnvilProfileForm = { 0.45f, 0.42f, -0.90f };

        PROPERTY( DisplayName( "Base Gradient Power" ), Category( "Shape" ), Range( 0.5f, 6.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Flatness of the cloud bottom." ) )
        float BaseGradientPower = 2.00f;

        PROPERTY( DisplayName( "Top Gradient Power" ), Category( "Shape" ), Range( 0.5f, 6.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Roundness of the cloud top." ) )
        float TopGradientPower = 1.50f;

        PROPERTY( DisplayName( "Density Height Bias" ), Category( "Shape" ), Range( 0.0f, 2.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Density increase with altitude inside the layer - heavy tops." ) )
        float DensityHeightBias = 0.70f;

        // ---- Detail -----------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Detail Strength" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Overall erosion by the detail volume: cauliflower versus smooth." ) )
        float DetailStrength = 0.75f;

        PROPERTY( DisplayName( "Detail Tile Size" ), Category( "Detail" ), Length,
                  Range( Common::Units::Metres( 200.0f ), Common::Units::Metres( 30000.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "World size of one detail tile, i.e. the size of a lobe." ) )
        float DetailTileSize = Common::Units::Metres( 2000.0f );

        PROPERTY( DisplayName( "Detail Seed" ), Category( "Detail" ), Range( 0, 65535 ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Regenerates the detail volume. Changing it re-bakes the noise." ) )
        int DetailSeed = 13;

        PROPERTY( DisplayName( "Detail Type Bias" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "0 = wispy erosion, 1 = billowy erosion." ) )
        float DetailTypeBias = 0.50f;

        PROPERTY( DisplayName( "Billow Gradient Power" ), Category( "Detail" ), Range( 0.05f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "How fast billow takes over from wisp as density rises." ) )
        float BillowGradientPower = 0.25f;

        PROPERTY( DisplayName( "Billow Noise Scale" ), Category( "Detail" ), Range( 0.0f, 1.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Strength of the billow channels of the detail noise." ) )
        float BillowNoiseScale = 0.30f;

        PROPERTY( DisplayName( "High Frequency Strength" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "The extra close-range detail layer." ) )
        float HighFreqStrength = 0.50f;

        PROPERTY( DisplayName( "High Frequency Wisp Sharpness" ), Category( "Detail" ), Range( 1.0f, 10.0f ),
                  Advanced, EditCondition( "Enabled" ), Tooltip( "Ridge sharpness of the high-frequency wisps." ) )
        float HighFreqWispSharpness = 4.00f;

        PROPERTY( DisplayName( "High Frequency Billow Sharpness" ), Category( "Detail" ), Range( 1.0f, 10.0f ),
                  Advanced, EditCondition( "Enabled" ), Tooltip( "Sharpness of the high-frequency billows." ) )
        float HighFreqBillowSharpness = 2.00f;

        // REPLACES High Frequency Fade Start / End, which were two authored distances describing a
        // relation neither of them could see. The band survives where the march's own step is fine
        // enough to resolve it and nowhere else, and that is a function of the quality tier as much as
        // of distance — the same 2.5-9 km fade was four times oversampled on Ultra and below Nyquist on
        // Low. The fade is now DERIVED from this size and the local step length (CloudNyquistWeight in
        // Common/CloudGeometry.glslh), so there is one number to author and it is a physical one: how
        // big the finest cauliflower creases are.
        PROPERTY( DisplayName( "High Frequency Feature Size" ), Category( "Detail" ), Length,
                  Range( Common::Units::Metres( 5.0f ), Common::Units::Metres( 400.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Size of the finest creases the near-field detail band carries. The band "
                           "fades itself out wherever the march's step is too coarse to resolve it." ) )
        float HighFreqFeatureSize = Common::Units::Metres( 45.0f );

        PROPERTY( DisplayName( "Curl Strength" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Curl-noise warp of the detail lookup: swirls and turbulent edges." ) )
        float CurlStrength = 0.35f;

        PROPERTY( DisplayName( "Curl Tile Size" ), Category( "Detail" ), Length,
                  Range( Common::Units::Metres( 500.0f ), Common::Units::Metres( 60000.0f ) ),
                  EditCondition( "Enabled" ), Tooltip( "World size of one curl swirl." ) )
        float CurlTileSize = Common::Units::Metres( 9000.0f );

        PROPERTY( DisplayName( "Density Sharpen Low" ), Category( "Detail" ), Range( 0.05f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Contrast exponent applied to thin regions." ) )
        float DensitySharpenLow = 0.30f;

        PROPERTY( DisplayName( "Density Sharpen High" ), Category( "Detail" ), Range( 0.05f, 2.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Contrast exponent applied to dense regions." ) )
        float DensitySharpenHigh = 0.60f;

        PROPERTY( DisplayName( "Density Scale Power" ), Category( "Detail" ), Range( 1.0f, 8.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Contrast of the density-scale channel." ) )
        float DensityScalePower = 4.00f;

        PROPERTY( DisplayName( "Distance Softening" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Attenuates detail with distance to kill aliasing." ) )
        float DistanceSoftening = 0.60f;

        PROPERTY( DisplayName( "Softening Start Distance" ), Category( "Detail" ), Length,
                  Range( 0.0f, Common::Units::Metres( 200000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which detail softening begins." ) )
        float SofteningStartDistance = Common::Units::Metres( 8000.0f );

        PROPERTY( DisplayName( "Softening End Distance" ), Category( "Detail" ), Length,
                  Range( 0.0f, Common::Units::Metres( 200000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which detail softening is at full strength." ) )
        float SofteningEndDistance = Common::Units::Metres( 45000.0f );

        PROPERTY( DisplayName( "Near Fade Start" ), Category( "Detail" ), Length,
                  Range( 0.0f, Common::Units::Metres( 5000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance in front of the camera where density starts fading in." ) )
        float NearFadeStart = Common::Units::Metres( 50.0f );

        PROPERTY( DisplayName( "Near Fade End" ), Category( "Detail" ), Length,
                  Range( 0.0f, Common::Units::Metres( 5000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which density is back to 100 per cent." ) )
        float NearFadeEnd = Common::Units::Metres( 900.0f );

        PROPERTY( DisplayName( "Near Fade Min Density" ), Category( "Detail" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Density fraction at Near Fade Start. Stops the screen filling with white when "
                           "the camera flies through a cloud." ) )
        float NearFadeMinDensity = 0.25f;

        // ---- Lighting ---------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Scattering Albedo" ), Category( "Lighting" ), Color, EditCondition( "Enabled" ),
                  Tooltip( "Single-scattering albedo. Below white it makes clouds dirty and grey." ) )
        glm::vec3 ScatteringAlbedo = { 1.0f, 1.0f, 1.0f };

        PROPERTY( DisplayName( "Extinction Tint" ), Category( "Lighting" ), Color, EditCondition( "Enabled" ),
                  Tooltip( "Per-channel extinction: a subtle colour shift through thickness." ) )
        glm::vec3 ExtinctionTint = { 1.0f, 1.0f, 1.0f };

        PROPERTY( DisplayName( "Light March Distance" ), Category( "Lighting" ), Length,
                  Range( Common::Units::Metres( 50.0f ), Common::Units::Metres( 6000.0f ) ),
                  EditCondition( "Enabled" ),
                  Tooltip( "How far the shadow ray reaches toward the sun. Long gives deeper shadows." ) )
        float LightMarchDistance = Common::Units::Metres( 1000.0f );

        PROPERTY( DisplayName( "Light Cone Spread" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Cone half-width of the shadow samples. Softens self-shadowing." ) )
        float LightConeSpread = 0.35f;

        PROPERTY( DisplayName( "Phase Forward G" ), Category( "Lighting" ), Range( 0.0f, 0.99f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Forward Henyey-Greenstein lobe: the silver-lining peak around the sun." ) )
        float PhaseForwardG = 0.80f;

        PROPERTY( DisplayName( "Phase Backward G" ), Category( "Lighting" ), Range( -0.99f, 0.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Backward lobe: brightness when looking away from the sun." ) )
        float PhaseBackwardG = -0.15f;

        PROPERTY( DisplayName( "Phase Blend" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Mix between the forward and backward lobes." ) )
        float PhaseBlend = 0.50f;

        PROPERTY( DisplayName( "Silver Lining Intensity" ), Category( "Lighting" ), Range( 0.0f, 4.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Multiplier on the forward lobe only. Pushes the rim glow." ) )
        float SilverLiningIntensity = 1.20f;

        PROPERTY( DisplayName( "Powder Strength" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Beer-powder dark-edge effect on cloud rims." ) )
        float PowderStrength = 0.50f;

        PROPERTY( DisplayName( "Powder Scale" ), Category( "Lighting" ), Range( 0.1f, 10.0f ), Advanced,
                  EditCondition( "Enabled" ), Tooltip( "Depth over which powder darkening acts." ) )
        float PowderScale = 2.00f;

        PROPERTY( DisplayName( "Multi Scatter Extinction Falloff" ), Category( "Lighting" ), Range( 0.05f, 1.0f ),
                  Advanced, EditCondition( "Enabled" ),
                  Tooltip( "Per-octave extinction decay of the multiple-scattering approximation." ) )
        float MultiScatterExtinctionFalloff = 0.50f;

        PROPERTY( DisplayName( "Multi Scatter Scatter Falloff" ), Category( "Lighting" ), Range( 0.05f, 1.0f ),
                  Advanced, EditCondition( "Enabled" ),
                  Tooltip( "Per-octave scattering decay of the multiple-scattering approximation." ) )
        float MultiScatterScatterFalloff = 0.50f;

        PROPERTY( DisplayName( "Multi Scatter Phase Falloff" ), Category( "Lighting" ), Range( 0.05f, 1.0f ),
                  Advanced, EditCondition( "Enabled" ),
                  Tooltip( "Per-octave phase-anisotropy decay: later octaves flatten out." ) )
        float MultiScatterPhaseFalloff = 0.50f;

        PROPERTY( DisplayName( "Ambient Sky Contribution" ), Category( "Lighting" ), Range( 0.0f, 3.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Multiplier on the sky ambient term taken from the atmosphere." ) )
        float AmbientSkyContribution = 1.20f;

        PROPERTY( DisplayName( "Ambient Ground Contribution" ), Category( "Lighting" ), Range( 0.0f, 3.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Multiplier on the ground-bounce ambient term." ) )
        float AmbientGroundContribution = 0.55f;

        PROPERTY( DisplayName( "Ambient Height Bias" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "How much ambient favours cloud tops over cloud bases." ) )
        float AmbientHeightBias = 0.75f;

        PROPERTY( DisplayName( "Sun Light Intensity Scale" ), Category( "Lighting" ), Range( 0.0f, 4.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Multiplier on the sun radiance supplied by the atmosphere - clouds only." ) )
        float SunLightIntensityScale = 1.00f;

        PROPERTY( DisplayName( "Sun Tint" ), Category( "Lighting" ), Color, EditCondition( "Enabled" ),
                  Tooltip( "Artistic tint of the lit side. An override on the atmosphere's sun colour, not "
                           "a replacement for it." ) )
        glm::vec3 SunTint = { 1.00f, 0.98f, 0.94f };

        PROPERTY( DisplayName( "Shadow Tint" ), Category( "Lighting" ), Color, EditCondition( "Enabled" ),
                  Tooltip( "Artistic tint of the shadowed side." ) )
        glm::vec3 ShadowTint = { 1.00f, 0.97f, 0.93f };

        PROPERTY( DisplayName( "Precipitation Darkening" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "How much Wetness darkens the cloud base." ) )
        float PrecipitationDarkening = 0.50f;

        PROPERTY( DisplayName( "Atmospheric Perspective" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ), Tooltip( "How much sky colour bleeds into distant clouds." ) )
        float AtmosphericPerspective = 0.80f;

        PROPERTY( DisplayName( "Distance Fade Start" ), Category( "Lighting" ), Length,
                  Range( 0.0f, Common::Units::Metres( 400000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which atmospheric blending begins." ) )
        float DistanceFadeStart = Common::Units::Metres( 50000.0f );

        PROPERTY( DisplayName( "Distance Fade End" ), Category( "Lighting" ), Length,
                  Range( 0.0f, Common::Units::Metres( 400000.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Distance at which clouds are fully the colour of the sky." ) )
        float DistanceFadeEnd = Common::Units::Metres( 140000.0f );

        // ---- Animation --------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Animation Speed" ), Category( "Animation" ), Range( 0.0f, 5.0f ), Units( "x" ),
                  EditCondition( "Enabled" ), Tooltip( "Global time multiplier. 0 freezes the sky." ) )
        float AnimationSpeed = 1.00f;

        PROPERTY( DisplayName( "Wind Influence" ), Category( "Animation" ), Range( 0.0f, 3.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Scale on the scene-global wind. Clouds never author a second wind: one "
                           "direction moves grass and clouds alike." ) )
        float WindInfluence = 1.00f;

        PROPERTY( DisplayName( "Wind Direction Offset" ), Category( "Animation" ), Range( -180.0f, 180.0f ),
                  Units( "deg" ), EditCondition( "Enabled" ),
                  Tooltip( "Rotates cloud drift relative to the grass and foliage wind." ) )
        float WindDirectionOffset = 0.00f;

        PROPERTY( DisplayName( "Shape Scroll Multiplier" ), Category( "Animation" ), Range( 0.0f, 5.0f ),
                  EditCondition( "Enabled" ), Tooltip( "Drift speed of the base shape lookup." ) )
        float ShapeScrollMultiplier = 1.00f;

        PROPERTY( DisplayName( "Detail Scroll Multiplier" ), Category( "Animation" ), Range( 0.0f, 8.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Detail drifts faster than shape - the classic boiling look." ) )
        float DetailScrollMultiplier = 2.00f;

        PROPERTY( DisplayName( "Weather Scroll Multiplier" ), Category( "Animation" ), Range( 0.0f, 3.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Speed at which whole cloud systems move across the sky." ) )
        float WeatherScrollMultiplier = 0.35f;

        PROPERTY( DisplayName( "Wind Height Shear" ), Category( "Animation" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "Higher altitudes drift faster, giving leaning, sheared clouds." ) )
        float WindHeightShear = 0.30f;

        PROPERTY( DisplayName( "Wind Uplift Speed" ), Category( "Animation" ), Length,
                  Range( 0.0f, Common::Units::Metres( 60.0f ) ), EditCondition( "Enabled" ),
                  Tooltip( "Vertical drift of the detail lookup per second - convective churn." ) )
        float WindUpliftSpeed = Common::Units::Metres( 4.0f );

        // ---- Quality ----------------------------------------------------------------------------------
        // A tier, not a multiplier: selecting anything but Custom overwrites the thirteen knobs below.
        // These are performance settings and no weather preset ever touches them - mixing the two is how
        // "Storm" ends up silently halving the frame rate.

        PROPERTY( DisplayName( "Quality Level" ), Category( "Quality" ),
                  Tooltip( "Selects a raymarch quality tier. Anything but Custom overwrites the settings "
                           "below." ) )
        CloudQuality QualityLevel = CloudQuality::High;

        PROPERTY( DisplayName( "Resolution Scale" ), Category( "Quality" ),
                  Tooltip( "Size of the raymarch buffer relative to the render target." ) )
        CloudResolutionScale ResolutionScale = CloudResolutionScale::Half;

        PROPERTY( DisplayName( "Max Steps" ), Category( "Quality" ), Range( 8, 512 ),
                  Tooltip( "Hard iteration cap of the view march." ) )
        int MaxSteps = 176;

        PROPERTY( DisplayName( "Min Step Size" ), Category( "Quality" ), Length,
                  Range( Common::Units::Metres( 1.0f ), Common::Units::Metres( 500.0f ) ),
                  Tooltip( "Finest step taken inside a cloud." ) )
        float MinStepSize = Common::Units::Metres( 15.0f );

        PROPERTY( DisplayName( "Max Step Size" ), Category( "Quality" ), Length,
                  Range( Common::Units::Metres( 50.0f ), Common::Units::Metres( 5000.0f ) ),
                  Tooltip( "Coarsest step taken far from the camera." ) )
        float MaxStepSize = Common::Units::Metres( 700.0f );

        PROPERTY( DisplayName( "Step Growth Rate" ), Category( "Quality" ), Range( 0.0f, 0.1f ),
                  Tooltip( "How fast the step size grows with distance." ) )
        float StepGrowthRate = 0.008f;

        PROPERTY( DisplayName( "Coarse Step Multiplier" ), Category( "Quality" ), Range( 1.0f, 8.0f ),
                  Tooltip( "Stride multiplier used while skipping empty space." ) )
        float CoarseStepMultiplier = 4.00f;

        PROPERTY( DisplayName( "Empty Samples Before Coarse" ), Category( "Quality" ), Range( 1, 32 ),
                  Tooltip( "Consecutive empty fine samples before returning to the coarse stride." ) )
        int EmptySamplesBeforeCoarse = 8;

        PROPERTY( DisplayName( "Light March Samples" ), Category( "Quality" ), Range( 1, 16 ),
                  Tooltip( "Shadow-ray samples per shaded sample." ) )
        int LightMarchSamples = 4;

        PROPERTY( DisplayName( "Auto Distance Fade" ), Category( "Lighting" ), EditCondition( "Enabled" ),
                  Tooltip( "Derive the aerial-perspective and horizon fade ranges from the layer's own "
                           "geometry instead of the authored kilometres. Distances along a ray grow as "
                           "1/sin(elevation), so a hand-authored 50-140 km range covers only the last "
                           "degree above the horizon; the derived one spans about thirty degrees down to "
                           "the layer's geometric horizon." ) )
        bool AutoDistanceFade = true;

        PROPERTY( DisplayName( "Ambient Occlusion" ), Category( "Lighting" ), Range( 0.0f, 1.0f ),
                  EditCondition( "Enabled" ),
                  Tooltip( "How much the sky's light is blocked by the cloud around and above a sample. At "
                           "0 every point in a cloud is lit by the full sky however deep it sits, which is "
                           "what renders a volume as a flat slab. Nubis3 pp. 141/144." ) )
        // 0.95, not the 0.55 this shipped with. An ablation on Clouds_UEShowcase — the same frame with
        // the ambient term forced to zero — showed the cloud radiance running about 2:1 ambient to
        // direct, and the clouds recovering deep shaded bellies and real form the moment the ambient
        // wash was removed. That wash is the whole of the "flat opaque lump" reading. The occlusion the
        // paper defines is what is supposed to prevent it, and taking it at barely half strength was the
        // audit's own finding (Nubis3 pp. 141/144: at 0.55 nearly half the unoccluded sky still reaches
        // a three-kilometre core). This is the Partly Cloudy row and the presets author it per weather;
        // the blankets sit lower, see CloudPresets.hpp.
        float AmbientOcclusion = 0.95f;

        PROPERTY( DisplayName( "Cloud Shadow Map" ), Category( "Quality" ),
                  Tooltip( "Precompute the sun's optical depth into a 2D map once per frame instead of "
                           "marching a cone of samples from every shaded point. The cone is twelve of "
                           "the eighteen texture fetches a shaded sample costs; the map is one. Its cost "
                           "is fixed rather than proportional to how much cloud is on screen. Turn off "
                           "to compare - the cone softens a self-shadow terminator slightly more." ) )
        bool CloudShadowMap = true;

        PROPERTY( DisplayName( "Cloud Shadow Extent" ), Category( "Quality" ), Length,
                  Range( Common::Units::Metres( 2000.0f ), Common::Units::Metres( 200000.0f ) ),
                  EditCondition( "CloudShadowMap" ),
                  Tooltip( "Half-width of the shadow map around the camera. Smaller is sharper and "
                           "covers less sky; clouds beyond it fall back to the cone march." ) )
        float CloudShadowExtent = Common::Units::Metres( 30000.0f );

        PROPERTY( DisplayName( "Multi Scatter Octaves" ), Category( "Quality" ), Range( 1, 4 ),
                  Tooltip( "Multiple-scattering octaves. Arithmetic only - no extra rays are traced." ) )
        int MultiScatterOctaves = 3;

        PROPERTY( DisplayName( "Temporal Mode" ), Category( "Quality" ),
                  Tooltip( "Temporal accumulation on or off. Off marches every frame in full." ) )
        CloudTemporalMode TemporalMode = CloudTemporalMode::Reprojection;

        PROPERTY( DisplayName( "Temporal Blend Factor" ), Category( "Quality" ), Range( 0.02f, 1.0f ),
                  Tooltip( "Weight of the current frame. Low is smoother but ghosts more." ) )
        float TemporalBlendFactor = 0.10f;

        PROPERTY( DisplayName( "Temporal Clamp Scale" ), Category( "Quality" ), Range( 0.5f, 4.0f ),
                  Tooltip( "Width of the neighbourhood clamp box. Low ghosts less but flickers more." ) )
        float TemporalClampScale = 1.50f;

        PROPERTY( DisplayName( "Jitter Strength" ), Category( "Quality" ), Range( 0.0f, 1.0f ),
                  Tooltip( "Per-pixel dither of the ray origin." ) )
        float JitterStrength = 1.00f;

        // ---- Preset -----------------------------------------------------------------------------------

        PROPERTY( DisplayName( "Preset" ), Category( "Preset" ),
                  Tooltip( "Selecting a preset applies a whole weather look. Editing any preset-driven "
                           "field afterwards sets this back to Custom." ) )
        CloudPreset Preset = CloudPreset::PartlyCloudy;
    };

    struct VolumetricCloudsComponent
    {
        VolumetricCloudData Data;

        // Transient (no PROPERTY -> not reflected, not serialized): raised for one frame when a seed
        // changes or the editor asks for it, consumed by the noise-generation pass.
        bool RequestRegenerateNoise = false;
    };
} // namespace Desert::ECS
