#pragma once

#include <Common/Core/Units.hpp>

#include <Engine/ECS/VolumetricCloudsComponent.hpp>

namespace Desert::Graphic
{
    // Weather presets: one constexpr table of VALUES, in this one file. Adding a preset is one
    // enumerator on ECS::CloudPreset plus one row below - there is no switch anywhere else to update,
    // which is the only version of "cheap to extend" that survives contact with a second developer.
    //
    // The table holds values rather than the apply-functions the particle presets use
    // (ParticleEditorPanel.cpp) for one reason worth the extra type: CloudPresetValues carries EXACTLY
    // the 78 look fields and none of the 13 quality knobs, so a preset cannot reach a performance
    // setting - not by convention, by type. That is what stops "Storm" from silently halving the frame
    // rate the way the reference implementation's combined preset/quality block did.
    //
    // Presets are NEVER re-applied on load: the serialized field values are the truth and
    // VolumetricCloudData::Preset is only a label. Editing this table therefore cannot change how an
    // already-authored scene looks.

    // The preset-driven field set, written ONCE. The value struct, the copy out of a component and the
    // copy back into one are all generated from this list, so they cannot drift apart - a field present
    // in the struct but forgotten by the apply loop is the exact bug that makes one slider of a preset
    // silently stop working.
#define DESERT_CLOUD_PRESET_FIELDS( X )                                                                           \
    X( float, LayerBottomAltitude )                                                                               \
    X( float, LayerThickness )                                                                                    \
    X( float, HorizonFadeStart )                                                                                  \
    X( float, HorizonFadeEnd )                                                                                    \
    X( float, Coverage )                                                                                          \
    X( float, CoverageContrast )                                                                                  \
    X( float, WeatherTileSize )                                                                                   \
    X( int, WeatherSeed )                                                                                         \
    X( int, WeatherOctaves )                                                                                      \
    X( float, WeatherWarpStrength )                                                                               \
    X( float, CloudType )                                                                                         \
    X( float, CloudTypeVariance )                                                                                 \
    X( float, AnvilBias )                                                                                         \
    X( float, Wetness )                                                                                           \
    X( float, ShapeTileSize )                                                                                     \
    X( int, ShapeSeed )                                                                                           \
    X( float, BaseShapeRemapMin )                                                                                 \
    X( float, ShapeErosionStrength )                                                                              \
    X( float, ExtinctionScale )                                                                                   \
    X( glm::vec4, StratusGradient )                                                                               \
    X( glm::vec4, StratocumulusGradient )                                                                         \
    X( glm::vec4, CumulusGradient )                                                                               \
    X( float, BaseGradientPower )                                                                                 \
    X( float, TopGradientPower )                                                                                  \
    X( float, DensityHeightBias )                                                                                 \
    X( float, DetailStrength )                                                                                    \
    X( float, DetailTileSize )                                                                                    \
    X( int, DetailSeed )                                                                                          \
    X( float, DetailTypeBias )                                                                                    \
    X( float, BillowGradientPower )                                                                               \
    X( float, BillowNoiseScale )                                                                                  \
    X( float, HighFreqStrength )                                                                                  \
    X( float, HighFreqWispSharpness )                                                                             \
    X( float, HighFreqBillowSharpness )                                                                           \
    X( float, HighFreqFadeStart )                                                                                 \
    X( float, HighFreqFadeEnd )                                                                                   \
    X( float, CurlStrength )                                                                                      \
    X( float, CurlTileSize )                                                                                      \
    X( float, DensitySharpenLow )                                                                                 \
    X( float, DensitySharpenHigh )                                                                                \
    X( float, DensityScalePower )                                                                                 \
    X( float, DistanceSoftening )                                                                                 \
    X( float, SofteningStartDistance )                                                                            \
    X( float, SofteningEndDistance )                                                                              \
    X( float, NearFadeStart )                                                                                     \
    X( float, NearFadeEnd )                                                                                       \
    X( float, NearFadeMinDensity )                                                                                \
    X( glm::vec3, ScatteringAlbedo )                                                                              \
    X( glm::vec3, ExtinctionTint )                                                                                \
    X( float, LightMarchDistance )                                                                                \
    X( float, LightConeSpread )                                                                                   \
    X( float, PhaseForwardG )                                                                                     \
    X( float, PhaseBackwardG )                                                                                    \
    X( float, PhaseBlend )                                                                                        \
    X( float, SilverLiningIntensity )                                                                             \
    X( float, PowderStrength )                                                                                    \
    X( float, PowderScale )                                                                                       \
    X( float, MultiScatterExtinctionFalloff )                                                                     \
    X( float, MultiScatterScatterFalloff )                                                                        \
    X( float, MultiScatterPhaseFalloff )                                                                          \
    X( float, AmbientSkyContribution )                                                                            \
    X( float, AmbientGroundContribution )                                                                         \
    X( float, AmbientHeightBias )                                                                                 \
    X( float, SunLightIntensityScale )                                                                            \
    X( glm::vec3, SunTint )                                                                                       \
    X( glm::vec3, ShadowTint )                                                                                    \
    X( float, PrecipitationDarkening )                                                                            \
    X( float, AtmosphericPerspective )                                                                            \
    X( float, DistanceFadeStart )                                                                                 \
    X( float, DistanceFadeEnd )                                                                                   \
    X( float, AnimationSpeed )                                                                                    \
    X( float, WindInfluence )                                                                                     \
    X( float, WindDirectionOffset )                                                                               \
    X( float, ShapeScrollMultiplier )                                                                             \
    X( float, DetailScrollMultiplier )                                                                            \
    X( float, WeatherScrollMultiplier )                                                                           \
    X( float, WindHeightShear )                                                                                   \
    X( float, WindUpliftSpeed )

    // The 78 fields a weather preset drives. Deliberately NOT the component: the quality group is
    // unreachable from here.
    struct CloudPresetValues
    {
#define DESERT_CLOUD_PRESET_DECLARE( Type, Name ) Type Name{};
        DESERT_CLOUD_PRESET_FIELDS( DESERT_CLOUD_PRESET_DECLARE )
#undef DESERT_CLOUD_PRESET_DECLARE

        bool operator==( const CloudPresetValues& ) const = default;
    };

    struct CloudPresetEntry
    {
        ECS::CloudPreset  Id;
        const char*       Name;
        CloudPresetValues Values;
    };

    // One row per enumerator of ECS::CloudPreset except Custom, which is the absence of a preset and so
    // has no values. The defaults of VolumetricCloudData are the Partly Cloudy row (asserted by test).
    inline constexpr CloudPresetEntry kCloudPresets[] = {
         { ECS::CloudPreset::Clear, "Clear",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 2000.0f ),
                .LayerThickness                = Common::Units::Metres( 2000.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 60000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 140000.0f ),
                .Coverage                      = 0.28f,
                .CoverageContrast              = 2.20f,
                .WeatherTileSize               = Common::Units::Metres( 60000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 5,
                .WeatherWarpStrength           = 0.35f,
                .CloudType                     = 0.75f,
                .CloudTypeVariance             = 0.25f,
                .AnvilBias                     = 0.00f,
                .Wetness                       = 0.00f,
                .ShapeTileSize                 = Common::Units::Metres( 35000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.42f,
                .ShapeErosionStrength          = 0.55f,
                .ExtinctionScale               = 0.70f,
                .StratusGradient               = { 0.0f, 0.08f, 0.20f, 0.32f },
                .StratocumulusGradient         = { 0.0f, 0.18f, 0.55f, 0.78f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 2.00f,
                .TopGradientPower              = 1.50f,
                .DensityHeightBias             = 0.50f,
                .DetailStrength                = 0.30f,
                .DetailTileSize                = Common::Units::Metres( 4000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.60f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.30f,
                .HighFreqStrength              = 0.40f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 2500.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 9000.0f ),
                .CurlStrength                  = 0.25f,
                .CurlTileSize                  = Common::Units::Metres( 9000.0f ),
                .DensitySharpenLow             = 0.30f,
                .DensitySharpenHigh            = 0.60f,
                .DensityScalePower             = 4.00f,
                .DistanceSoftening             = 0.60f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 900.0f ),
                .NearFadeMinDensity            = 0.25f,
                .ScatteringAlbedo              = { 1.0f, 1.0f, 1.0f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 800.0f ),
                .LightConeSpread               = 0.35f,
                .PhaseForwardG                 = 0.80f,
                .PhaseBackwardG                = -0.15f,
                .PhaseBlend                    = 0.50f,
                .SilverLiningIntensity         = 1.00f,
                .PowderStrength                = 0.50f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.50f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 1.80f,
                .AmbientGroundContribution     = 0.55f,
                .AmbientHeightBias             = 0.50f,
                .SunLightIntensityScale        = 1.00f,
                .SunTint                       = { 1.00f, 0.98f, 0.94f },
                .ShadowTint                    = { 1.00f, 0.97f, 0.93f },
                .PrecipitationDarkening        = 0.50f,
                .AtmosphericPerspective        = 0.80f,
                .DistanceFadeStart             = Common::Units::Metres( 50000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 140000.0f ),
                .AnimationSpeed                = 1.00f,
                .WindInfluence                 = 1.00f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 2.00f,
                .WeatherScrollMultiplier       = 0.35f,
                .WindHeightShear               = 0.30f,
                .WindUpliftSpeed               = Common::Units::Metres( 4.0f ),
           } },
         { ECS::CloudPreset::FairWeather, "Fair Weather",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 1500.0f ),
                .LayerThickness                = Common::Units::Metres( 2500.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 60000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 140000.0f ),
                .Coverage                      = 0.62f,
                .CoverageContrast              = 1.60f,
                .WeatherTileSize               = Common::Units::Metres( 50000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 5,
                .WeatherWarpStrength           = 0.40f,
                .CloudType                     = 0.70f,
                .CloudTypeVariance             = 0.35f,
                .AnvilBias                     = 0.00f,
                .Wetness                       = 0.05f,
                .ShapeTileSize                 = Common::Units::Metres( 35000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.36f,
                .ShapeErosionStrength          = 0.60f,
                .ExtinctionScale               = 0.85f,
                .StratusGradient               = { 0.0f, 0.08f, 0.20f, 0.32f },
                .StratocumulusGradient         = { 0.0f, 0.18f, 0.55f, 0.78f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 2.00f,
                .TopGradientPower              = 1.50f,
                .DensityHeightBias             = 0.60f,
                .DetailStrength                = 0.34f,
                .DetailTileSize                = Common::Units::Metres( 4000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.55f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.30f,
                .HighFreqStrength              = 0.45f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 2500.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 9000.0f ),
                .CurlStrength                  = 0.30f,
                .CurlTileSize                  = Common::Units::Metres( 9000.0f ),
                .DensitySharpenLow             = 0.30f,
                .DensitySharpenHigh            = 0.60f,
                .DensityScalePower             = 4.00f,
                .DistanceSoftening             = 0.60f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 900.0f ),
                .NearFadeMinDensity            = 0.25f,
                .ScatteringAlbedo              = { 1.0f, 1.0f, 1.0f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 900.0f ),
                .LightConeSpread               = 0.35f,
                .PhaseForwardG                 = 0.80f,
                .PhaseBackwardG                = -0.15f,
                .PhaseBlend                    = 0.50f,
                .SilverLiningIntensity         = 1.10f,
                .PowderStrength                = 0.50f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.50f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 1.80f,
                .AmbientGroundContribution     = 0.55f,
                .AmbientHeightBias             = 0.50f,
                .SunLightIntensityScale        = 1.00f,
                .SunTint                       = { 1.00f, 0.98f, 0.94f },
                .ShadowTint                    = { 1.00f, 0.97f, 0.93f },
                .PrecipitationDarkening        = 0.50f,
                .AtmosphericPerspective        = 0.80f,
                .DistanceFadeStart             = Common::Units::Metres( 50000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 140000.0f ),
                .AnimationSpeed                = 1.00f,
                .WindInfluence                 = 1.00f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 2.00f,
                .WeatherScrollMultiplier       = 0.35f,
                .WindHeightShear               = 0.30f,
                .WindUpliftSpeed               = Common::Units::Metres( 4.0f ),
           } },
         { ECS::CloudPreset::PartlyCloudy, "Partly Cloudy",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 1500.0f ),
                .LayerThickness                = Common::Units::Metres( 3500.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 60000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 140000.0f ),
                .Coverage                      = 0.80f,
                .CoverageContrast              = 1.20f,
                .WeatherTileSize               = Common::Units::Metres( 60000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 5,
                .WeatherWarpStrength           = 0.45f,
                .CloudType                     = 0.60f,
                .CloudTypeVariance             = 0.45f,
                .AnvilBias                     = 0.10f,
                .Wetness                       = 0.15f,
                .ShapeTileSize                 = Common::Units::Metres( 35000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.30f,
                .ShapeErosionStrength          = 0.65f,
                .ExtinctionScale               = 1.00f,
                .StratusGradient               = { 0.0f, 0.08f, 0.20f, 0.32f },
                .StratocumulusGradient         = { 0.0f, 0.18f, 0.55f, 0.78f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 2.00f,
                .TopGradientPower              = 1.50f,
                .DensityHeightBias             = 0.70f,
                .DetailStrength                = 0.38f,
                .DetailTileSize                = Common::Units::Metres( 4000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.50f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.30f,
                .HighFreqStrength              = 0.50f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 2500.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 9000.0f ),
                .CurlStrength                  = 0.35f,
                .CurlTileSize                  = Common::Units::Metres( 9000.0f ),
                .DensitySharpenLow             = 0.30f,
                .DensitySharpenHigh            = 0.60f,
                .DensityScalePower             = 4.00f,
                .DistanceSoftening             = 0.60f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 900.0f ),
                .NearFadeMinDensity            = 0.25f,
                .ScatteringAlbedo              = { 1.0f, 1.0f, 1.0f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 1000.0f ),
                .LightConeSpread               = 0.35f,
                .PhaseForwardG                 = 0.80f,
                .PhaseBackwardG                = -0.15f,
                .PhaseBlend                    = 0.50f,
                .SilverLiningIntensity         = 1.20f,
                .PowderStrength                = 0.50f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.50f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 1.80f,
                .AmbientGroundContribution     = 0.55f,
                .AmbientHeightBias             = 0.50f,
                .SunLightIntensityScale        = 1.00f,
                .SunTint                       = { 1.00f, 0.98f, 0.94f },
                .ShadowTint                    = { 1.00f, 0.97f, 0.93f },
                .PrecipitationDarkening        = 0.50f,
                .AtmosphericPerspective        = 0.80f,
                .DistanceFadeStart             = Common::Units::Metres( 50000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 140000.0f ),
                .AnimationSpeed                = 1.00f,
                .WindInfluence                 = 1.00f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 2.00f,
                .WeatherScrollMultiplier       = 0.35f,
                .WindHeightShear               = 0.30f,
                .WindUpliftSpeed               = Common::Units::Metres( 4.0f ),
           } },
         { ECS::CloudPreset::SummerCumulus, "Summer Cumulus",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 900.0f ),
                .LayerThickness                = Common::Units::Metres( 2600.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 60000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 140000.0f ),
                .Coverage                      = 0.86f,
                .CoverageContrast              = 1.05f,
                .WeatherTileSize               = Common::Units::Metres( 45000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 6,
                .WeatherWarpStrength           = 0.45f,
                .CloudType                     = 0.80f,
                .CloudTypeVariance             = 0.55f,
                .AnvilBias                     = 0.05f,
                .Wetness                       = 0.10f,
                .ShapeTileSize                 = Common::Units::Metres( 35000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.30f,
                .ShapeErosionStrength          = 0.65f,
                .ExtinctionScale               = 1.25f,
                .StratusGradient               = { 0.0f, 0.08f, 0.20f, 0.32f },
                .StratocumulusGradient         = { 0.0f, 0.18f, 0.55f, 0.78f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 3.20f,
                .TopGradientPower              = 1.20f,
                .DensityHeightBias             = 0.85f,
                .DetailStrength                = 0.52f,
                .DetailTileSize                = Common::Units::Metres( 4000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.72f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.38f,
                .HighFreqStrength              = 0.70f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 4000.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 16000.0f ),
                .CurlStrength                  = 0.45f,
                .CurlTileSize                  = Common::Units::Metres( 9000.0f ),
                .DensitySharpenLow             = 0.30f,
                .DensitySharpenHigh            = 0.60f,
                .DensityScalePower             = 4.00f,
                .DistanceSoftening             = 0.60f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 900.0f ),
                .NearFadeMinDensity            = 0.25f,
                .ScatteringAlbedo              = { 1.0f, 1.0f, 1.0f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 1000.0f ),
                .LightConeSpread               = 0.35f,
                .PhaseForwardG                 = 0.80f,
                .PhaseBackwardG                = -0.15f,
                .PhaseBlend                    = 0.50f,
                .SilverLiningIntensity         = 1.45f,
                .PowderStrength                = 0.65f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.50f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 1.60f,
                .AmbientGroundContribution     = 0.45f,
                .AmbientHeightBias             = 0.50f,
                .SunLightIntensityScale        = 1.00f,
                .SunTint                       = { 1.00f, 0.98f, 0.94f },
                .ShadowTint                    = { 1.00f, 0.97f, 0.93f },
                .PrecipitationDarkening        = 0.50f,
                .AtmosphericPerspective        = 0.80f,
                .DistanceFadeStart             = Common::Units::Metres( 50000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 140000.0f ),
                .AnimationSpeed                = 1.00f,
                .WindInfluence                 = 1.00f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 2.00f,
                .WeatherScrollMultiplier       = 0.35f,
                .WindHeightShear               = 0.30f,
                .WindUpliftSpeed               = Common::Units::Metres( 4.0f ),
           } },
         { ECS::CloudPreset::Stratus, "Stratus",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 600.0f ),
                .LayerThickness                = Common::Units::Metres( 700.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 45000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 110000.0f ),
                .Coverage                      = 0.90f,
                .CoverageContrast              = 0.70f,
                .WeatherTileSize               = Common::Units::Metres( 90000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 4,
                .WeatherWarpStrength           = 0.20f,
                .CloudType                     = 0.05f,
                .CloudTypeVariance             = 0.10f,
                .AnvilBias                     = 0.00f,
                .Wetness                       = 0.50f,
                .ShapeTileSize                 = Common::Units::Metres( 60000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.18f,
                .ShapeErosionStrength          = 0.35f,
                .ExtinctionScale               = 1.10f,
                .StratusGradient               = { 0.0f, 0.06f, 0.24f, 0.40f },
                .StratocumulusGradient         = { 0.0f, 0.16f, 0.50f, 0.70f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 2.60f,
                .TopGradientPower              = 1.80f,
                .DensityHeightBias             = 0.20f,
                .DetailStrength                = 0.18f,
                .DetailTileSize                = Common::Units::Metres( 6000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.15f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.26f,
                .HighFreqStrength              = 0.20f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 2500.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 9000.0f ),
                .CurlStrength                  = 0.15f,
                .CurlTileSize                  = Common::Units::Metres( 12000.0f ),
                .DensitySharpenLow             = 0.40f,
                .DensitySharpenHigh            = 0.70f,
                .DensityScalePower             = 2.50f,
                .DistanceSoftening             = 0.55f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 700.0f ),
                .NearFadeMinDensity            = 0.20f,
                .ScatteringAlbedo              = { 0.97f, 0.97f, 0.98f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 500.0f ),
                .LightConeSpread               = 0.30f,
                .PhaseForwardG                 = 0.72f,
                .PhaseBackwardG                = -0.12f,
                .PhaseBlend                    = 0.45f,
                .SilverLiningIntensity         = 0.60f,
                .PowderStrength                = 0.25f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.55f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 2.00f,
                .AmbientGroundContribution     = 0.60f,
                .AmbientHeightBias             = 0.40f,
                .SunLightIntensityScale        = 0.90f,
                .SunTint                       = { 1.0f, 1.0f, 1.0f },
                .ShadowTint                    = { 0.95f, 0.96f, 1.0f },
                .PrecipitationDarkening        = 0.60f,
                .AtmosphericPerspective        = 0.85f,
                .DistanceFadeStart             = Common::Units::Metres( 40000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 110000.0f ),
                .AnimationSpeed                = 0.50f,
                .WindInfluence                 = 0.80f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 1.40f,
                .WeatherScrollMultiplier       = 0.25f,
                .WindHeightShear               = 0.20f,
                .WindUpliftSpeed               = Common::Units::Metres( 1.0f ),
           } },
         { ECS::CloudPreset::Overcast, "Overcast",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 900.0f ),
                .LayerThickness                = Common::Units::Metres( 2200.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 50000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 120000.0f ),
                .Coverage                      = 0.95f,
                .CoverageContrast              = 0.60f,
                .WeatherTileSize               = Common::Units::Metres( 120000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 4,
                .WeatherWarpStrength           = 0.25f,
                .CloudType                     = 0.25f,
                .CloudTypeVariance             = 0.20f,
                .AnvilBias                     = 0.05f,
                .Wetness                       = 0.60f,
                .ShapeTileSize                 = Common::Units::Metres( 50000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.20f,
                .ShapeErosionStrength          = 0.40f,
                .ExtinctionScale               = 1.30f,
                .StratusGradient               = { 0.0f, 0.08f, 0.22f, 0.36f },
                .StratocumulusGradient         = { 0.0f, 0.18f, 0.58f, 0.82f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.70f, 0.94f },
                .BaseGradientPower             = 2.30f,
                .TopGradientPower              = 1.60f,
                .DensityHeightBias             = 0.35f,
                .DetailStrength                = 0.24f,
                .DetailTileSize                = Common::Units::Metres( 5000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.30f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.28f,
                .HighFreqStrength              = 0.30f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 2500.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 9000.0f ),
                .CurlStrength                  = 0.20f,
                .CurlTileSize                  = Common::Units::Metres( 11000.0f ),
                .DensitySharpenLow             = 0.36f,
                .DensitySharpenHigh            = 0.66f,
                .DensityScalePower             = 3.00f,
                .DistanceSoftening             = 0.55f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 800.0f ),
                .NearFadeMinDensity            = 0.22f,
                .ScatteringAlbedo              = { 0.96f, 0.96f, 0.98f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 700.0f ),
                .LightConeSpread               = 0.32f,
                .PhaseForwardG                 = 0.75f,
                .PhaseBackwardG                = -0.14f,
                .PhaseBlend                    = 0.48f,
                .SilverLiningIntensity         = 0.70f,
                .PowderStrength                = 0.30f,
                .PowderScale                   = 2.00f,
                .MultiScatterExtinctionFalloff = 0.55f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 2.20f,
                .AmbientGroundContribution     = 0.65f,
                .AmbientHeightBias             = 0.45f,
                .SunLightIntensityScale        = 0.85f,
                .SunTint                       = { 1.0f, 1.0f, 1.0f },
                .ShadowTint                    = { 0.92f, 0.94f, 1.0f },
                .PrecipitationDarkening        = 0.65f,
                .AtmosphericPerspective        = 0.85f,
                .DistanceFadeStart             = Common::Units::Metres( 45000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 120000.0f ),
                .AnimationSpeed                = 0.70f,
                .WindInfluence                 = 0.90f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 1.60f,
                .WeatherScrollMultiplier       = 0.30f,
                .WindHeightShear               = 0.25f,
                .WindUpliftSpeed               = Common::Units::Metres( 2.0f ),
           } },
         { ECS::CloudPreset::Storm, "Storm",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 700.0f ),
                .LayerThickness                = Common::Units::Metres( 9000.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 55000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 130000.0f ),
                .Coverage                      = 0.98f,
                .CoverageContrast              = 0.80f,
                .WeatherTileSize               = Common::Units::Metres( 70000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 6,
                .WeatherWarpStrength           = 0.60f,
                .CloudType                     = 0.90f,
                .CloudTypeVariance             = 0.50f,
                .AnvilBias                     = 0.75f,
                .Wetness                       = 1.00f,
                .ShapeTileSize                 = Common::Units::Metres( 30000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.24f,
                .ShapeErosionStrength          = 0.70f,
                .ExtinctionScale               = 2.00f,
                .StratusGradient               = { 0.0f, 0.08f, 0.20f, 0.32f },
                .StratocumulusGradient         = { 0.0f, 0.16f, 0.62f, 0.88f },
                .CumulusGradient               = { 0.0f, 0.15f, 0.80f, 1.0f },
                .BaseGradientPower             = 1.60f,
                .TopGradientPower              = 1.10f,
                .DensityHeightBias             = 1.10f,
                .DetailStrength                = 0.50f,
                .DetailTileSize                = Common::Units::Metres( 3000.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.70f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.38f,
                .HighFreqStrength              = 0.60f,
                .HighFreqWispSharpness         = 4.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 3000.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 11000.0f ),
                .CurlStrength                  = 0.55f,
                .CurlTileSize                  = Common::Units::Metres( 7000.0f ),
                .DensitySharpenLow             = 0.24f,
                .DensitySharpenHigh            = 0.52f,
                .DensityScalePower             = 4.50f,
                .DistanceSoftening             = 0.65f,
                .SofteningStartDistance        = Common::Units::Metres( 8000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 45000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 1200.0f ),
                .NearFadeMinDensity            = 0.30f,
                .ScatteringAlbedo              = { 0.92f, 0.93f, 0.96f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 1400.0f ),
                .LightConeSpread               = 0.40f,
                .PhaseForwardG                 = 0.80f,
                .PhaseBackwardG                = -0.25f,
                .PhaseBlend                    = 0.60f,
                .SilverLiningIntensity         = 1.40f,
                .PowderStrength                = 0.70f,
                .PowderScale                   = 2.40f,
                .MultiScatterExtinctionFalloff = 0.65f,
                .MultiScatterScatterFalloff    = 0.55f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 2.40f,
                .AmbientGroundContribution     = 0.70f,
                .AmbientHeightBias             = 0.60f,
                .SunLightIntensityScale        = 0.75f,
                .SunTint                       = { 1.0f, 1.0f, 1.0f },
                .ShadowTint                    = { 0.86f, 0.89f, 0.98f },
                .PrecipitationDarkening        = 0.85f,
                .AtmosphericPerspective        = 0.75f,
                .DistanceFadeStart             = Common::Units::Metres( 50000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 130000.0f ),
                .AnimationSpeed                = 2.20f,
                .WindInfluence                 = 1.80f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.20f,
                .DetailScrollMultiplier        = 3.00f,
                .WeatherScrollMultiplier       = 0.60f,
                .WindHeightShear               = 0.50f,
                .WindUpliftSpeed               = Common::Units::Metres( 18.0f ),
           } },
         { ECS::CloudPreset::Cirrus, "Cirrus",
           CloudPresetValues{
                .LayerBottomAltitude           = Common::Units::Metres( 8000.0f ),
                .LayerThickness                = Common::Units::Metres( 1200.0f ),
                .HorizonFadeStart              = Common::Units::Metres( 90000.0f ),
                .HorizonFadeEnd                = Common::Units::Metres( 200000.0f ),
                .Coverage                      = 0.66f,
                .CoverageContrast              = 1.90f,
                .WeatherTileSize               = Common::Units::Metres( 140000.0f ),
                .WeatherSeed                   = 1337,
                .WeatherOctaves                = 6,
                .WeatherWarpStrength           = 0.50f,
                .CloudType                     = 0.15f,
                .CloudTypeVariance             = 0.30f,
                .AnvilBias                     = 0.00f,
                .Wetness                       = 0.00f,
                .ShapeTileSize                 = Common::Units::Metres( 70000.0f ),
                .ShapeSeed                     = 7,
                .BaseShapeRemapMin             = 0.44f,
                .ShapeErosionStrength          = 0.80f,
                .ExtinctionScale               = 0.35f,
                .StratusGradient               = { 0.0f, 0.05f, 0.30f, 0.55f },
                .StratocumulusGradient         = { 0.0f, 0.14f, 0.60f, 0.85f },
                .CumulusGradient               = { 0.0f, 0.22f, 0.68f, 0.92f },
                .BaseGradientPower             = 2.00f,
                .TopGradientPower              = 2.20f,
                .DensityHeightBias             = 0.20f,
                .DetailStrength                = 0.55f,
                .DetailTileSize                = Common::Units::Metres( 2500.0f ),
                .DetailSeed                    = 13,
                .DetailTypeBias                = 0.05f,
                .BillowGradientPower           = 0.25f,
                .BillowNoiseScale              = 0.22f,
                .HighFreqStrength              = 0.70f,
                .HighFreqWispSharpness         = 5.00f,
                .HighFreqBillowSharpness       = 2.00f,
                .HighFreqFadeStart             = Common::Units::Metres( 4000.0f ),
                .HighFreqFadeEnd               = Common::Units::Metres( 14000.0f ),
                .CurlStrength                  = 0.65f,
                .CurlTileSize                  = Common::Units::Metres( 6000.0f ),
                .DensitySharpenLow             = 0.34f,
                .DensitySharpenHigh            = 0.64f,
                .DensityScalePower             = 4.00f,
                .DistanceSoftening             = 0.70f,
                .SofteningStartDistance        = Common::Units::Metres( 15000.0f ),
                .SofteningEndDistance          = Common::Units::Metres( 70000.0f ),
                .NearFadeStart                 = Common::Units::Metres( 50.0f ),
                .NearFadeEnd                   = Common::Units::Metres( 900.0f ),
                .NearFadeMinDensity            = 0.25f,
                .ScatteringAlbedo              = { 1.0f, 1.0f, 1.0f },
                .ExtinctionTint                = { 1.0f, 1.0f, 1.0f },
                .LightMarchDistance            = Common::Units::Metres( 400.0f ),
                .LightConeSpread               = 0.30f,
                .PhaseForwardG                 = 0.85f,
                .PhaseBackwardG                = -0.10f,
                .PhaseBlend                    = 0.55f,
                .SilverLiningIntensity         = 1.60f,
                .PowderStrength                = 0.40f,
                .PowderScale                   = 1.60f,
                .MultiScatterExtinctionFalloff = 0.45f,
                .MultiScatterScatterFalloff    = 0.50f,
                .MultiScatterPhaseFalloff      = 0.50f,
                .AmbientSkyContribution        = 1.70f,
                .AmbientGroundContribution     = 0.50f,
                .AmbientHeightBias             = 0.50f,
                .SunLightIntensityScale        = 1.10f,
                .SunTint                       = { 1.00f, 0.98f, 0.94f },
                .ShadowTint                    = { 1.00f, 0.97f, 0.93f },
                .PrecipitationDarkening        = 0.50f,
                .AtmosphericPerspective        = 0.60f,
                .DistanceFadeStart             = Common::Units::Metres( 80000.0f ),
                .DistanceFadeEnd               = Common::Units::Metres( 200000.0f ),
                .AnimationSpeed                = 1.60f,
                .WindInfluence                 = 1.30f,
                .WindDirectionOffset           = 0.00f,
                .ShapeScrollMultiplier         = 1.00f,
                .DetailScrollMultiplier        = 2.40f,
                .WeatherScrollMultiplier       = 0.50f,
                .WindHeightShear               = 0.15f,
                .WindUpliftSpeed               = Common::Units::Metres( 3.0f ),
           } },
    };

    // The preset row for @p id, or nullptr for Custom (and for any enumerator someone adds without a
    // row - the table-completeness test turns that into a failing test rather than a silent no-op).
    inline constexpr const CloudPresetEntry* FindCloudPreset( ECS::CloudPreset id )
    {
        for ( const CloudPresetEntry& entry : kCloudPresets )
            if ( entry.Id == id )
                return &entry;
        return nullptr;
    }

    // The preset-driven half of a component, lifted out. This is the value the editor compares before
    // and after drawing the Details block to decide whether a LOOK field was edited: comparing the
    // extracted struct is exact, where a hash of it could collide and leave a preset name that lies.
    inline CloudPresetValues ExtractPresetValues( const ECS::VolumetricCloudData& data )
    {
        CloudPresetValues values;
#define DESERT_CLOUD_PRESET_READ( Type, Name ) values.Name = data.Name;
        DESERT_CLOUD_PRESET_FIELDS( DESERT_CLOUD_PRESET_READ )
#undef DESERT_CLOUD_PRESET_READ
        return values;
    }

    // Overwrites the 78 look fields of @p data with the preset's. Pure: no logging, no GPU, no globals.
    //
    // It deliberately does NOT write data.Preset. The caller records which preset it applied, which is
    // what keeps the contract "values in, values out" and keeps "applying a preset leaves everything
    // else alone" a statement a test can actually make. Custom applies nothing: it is a label for
    // hand-authored values, not a set of them.
    inline void ApplyPreset( ECS::CloudPreset id, ECS::VolumetricCloudData& data )
    {
        const CloudPresetEntry* entry = FindCloudPreset( id );
        if ( !entry )
            return;

#define DESERT_CLOUD_PRESET_WRITE( Type, Name ) data.Name = entry->Values.Name;
        DESERT_CLOUD_PRESET_FIELDS( DESERT_CLOUD_PRESET_WRITE )
#undef DESERT_CLOUD_PRESET_WRITE
    }

    // Which preset these look values ARE, or Custom when they are none of them. Quality fields are not
    // consulted, so dropping the quality tier never costs the artist the name of their weather preset.
    inline ECS::CloudPreset MatchPreset( const ECS::VolumetricCloudData& data )
    {
        const CloudPresetValues values = ExtractPresetValues( data );
        for ( const CloudPresetEntry& entry : kCloudPresets )
            if ( entry.Values == values )
                return entry.Id;
        return ECS::CloudPreset::Custom;
    }

    // Display name for a combo box. Custom has no row, so it is named here and nowhere else.
    inline const char* CloudPresetName( ECS::CloudPreset id )
    {
        const CloudPresetEntry* entry = FindCloudPreset( id );
        return entry ? entry->Name : "Custom";
    }
} // namespace Desert::Graphic
