#pragma once

#include <Common/Core/Units.hpp>

#include <Engine/ECS/VolumetricCloudsComponent.hpp>

namespace Desert::Graphic
{
    // Raymarch quality tiers, in the same shape as the weather presets next door (CloudPresets.hpp) and
    // for the same reason: adding a tier is one enumerator plus one row, and CloudQualityValues carries
    // EXACTLY the thirteen performance knobs, so a tier cannot reach a look field any more than a preset
    // can reach a performance one. Look and cost are two separate dials on purpose - the reference
    // implementation put its cloud-type selector and its step-size constants in the same UI block, which
    // is how choosing "Storm" ends up costing frame rate that nobody asked it to spend.

    // The quality-driven field set, written ONCE - the struct, the read and the write below are all
    // generated from it, so the three cannot drift apart.
#define DESERT_CLOUD_QUALITY_FIELDS( X )                                                                          \
    X( ECS::CloudResolutionScale, ResolutionScale )                                                               \
    X( int, MaxSteps )                                                                                            \
    X( float, MinStepSize )                                                                                       \
    X( float, MaxStepSize )                                                                                       \
    X( float, StepGrowthRate )                                                                                    \
    X( float, CoarseStepMultiplier )                                                                              \
    X( int, EmptySamplesBeforeCoarse )                                                                            \
    X( int, LightMarchSamples )                                                                                   \
    X( int, MultiScatterOctaves )                                                                                 \
    X( ECS::CloudTemporalMode, TemporalMode )                                                                     \
    X( float, TemporalBlendFactor )                                                                               \
    X( float, TemporalClampScale )                                                                                \
    X( float, JitterStrength )

    // The thirteen knobs a quality tier drives. Deliberately NOT the component: the 78 look fields are
    // unreachable from here.
    struct CloudQualityValues
    {
#define DESERT_CLOUD_QUALITY_DECLARE( Type, Name ) Type Name{};
        DESERT_CLOUD_QUALITY_FIELDS( DESERT_CLOUD_QUALITY_DECLARE )
#undef DESERT_CLOUD_QUALITY_DECLARE

        bool operator==( const CloudQualityValues& ) const = default;
    };

    struct CloudQualityEntry
    {
        ECS::CloudQuality  Id;
        const char*        Name;
        CloudQualityValues Values;
    };

    // One row per enumerator of ECS::CloudQuality except Custom, which is the absence of a tier and so
    // has no values. The defaults of VolumetricCloudData are the High row (asserted by test).
    inline constexpr CloudQualityEntry kCloudQualityTiers[] = {
         { ECS::CloudQuality::Low, "Low",
           CloudQualityValues{
                .ResolutionScale          = ECS::CloudResolutionScale::Quarter,
                .MaxSteps                 = 32,
                .MinStepSize              = Common::Units::Metres( 60.0f ),
                .MaxStepSize              = Common::Units::Metres( 1500.0f ),
                .StepGrowthRate           = 0.020f,
                .CoarseStepMultiplier     = 4.0f,
                .EmptySamplesBeforeCoarse = 4,
                .LightMarchSamples        = 3,
                .MultiScatterOctaves      = 1,
                .TemporalMode             = ECS::CloudTemporalMode::Off,
                .TemporalBlendFactor      = 1.00f,
                .TemporalClampScale       = 1.00f,
                .JitterStrength           = 1.00f,
           } },
         { ECS::CloudQuality::Medium, "Medium",
           CloudQualityValues{
                .ResolutionScale          = ECS::CloudResolutionScale::Half,
                .MaxSteps                 = 64,
                .MinStepSize              = Common::Units::Metres( 30.0f ),
                .MaxStepSize              = Common::Units::Metres( 1000.0f ),
                .StepGrowthRate           = 0.012f,
                .CoarseStepMultiplier     = 3.0f,
                .EmptySamplesBeforeCoarse = 6,
                .LightMarchSamples        = 4,
                .MultiScatterOctaves      = 2,
                .TemporalMode             = ECS::CloudTemporalMode::Reprojection,
                .TemporalBlendFactor      = 0.15f,
                .TemporalClampScale       = 1.25f,
                .JitterStrength           = 1.00f,
           } },
         { ECS::CloudQuality::High, "High",
           CloudQualityValues{
                .ResolutionScale          = ECS::CloudResolutionScale::Half,
                .MaxSteps                 = 128,
                .MinStepSize              = Common::Units::Metres( 15.0f ),
                .MaxStepSize              = Common::Units::Metres( 700.0f ),
                .StepGrowthRate           = 0.008f,
                .CoarseStepMultiplier     = 3.0f,
                .EmptySamplesBeforeCoarse = 8,
                // 4, not 6. The cone march is 12 of the 18 texture fetches a shaded sample costs — two
                // per cone sample — so this one number is two thirds of the raymarch. Four keeps the
                // shadow terminator readable; Ultra below still authors 8 for stills and captures.
                .LightMarchSamples = 4,
                // 3 octaves since CLD-108: at tauSun >~ 3 two octaves both vanish and a storm interior
                // collapses onto ambient alone. The third costs arithmetic, not fetches.
                .MultiScatterOctaves = 3,
                .TemporalMode        = ECS::CloudTemporalMode::Reprojection,
                .TemporalBlendFactor = 0.10f,
                .TemporalClampScale  = 1.50f,
                .JitterStrength      = 1.00f,
           } },
         { ECS::CloudQuality::Ultra, "Ultra",
           CloudQualityValues{
                .ResolutionScale = ECS::CloudResolutionScale::Full,
                // 192/12 m/6, down from 256/8 m/8: measured by frame-count slope, the full-res march is
                // ~10x the High tier and these three knobs are most of it; the visual delta at full res
                // is inside what the temporal accumulation resolves anyway.
                .MaxSteps                 = 192,
                .MinStepSize              = Common::Units::Metres( 12.0f ),
                .MaxStepSize              = Common::Units::Metres( 500.0f ),
                .StepGrowthRate           = 0.006f,
                .CoarseStepMultiplier     = 2.0f,
                .EmptySamplesBeforeCoarse = 8,
                .LightMarchSamples        = 6,
                .MultiScatterOctaves      = 3,
                .TemporalMode             = ECS::CloudTemporalMode::Reprojection,
                .TemporalBlendFactor      = 0.08f,
                .TemporalClampScale       = 1.75f,
                .JitterStrength           = 0.50f,
           } },
    };

    // The tier row for @p id, or nullptr for Custom (and for any enumerator added without a row - the
    // completeness test turns that into a failing test rather than a menu entry that does nothing).
    inline constexpr const CloudQualityEntry* FindCloudQuality( ECS::CloudQuality id )
    {
        for ( const CloudQualityEntry& entry : kCloudQualityTiers )
            if ( entry.Id == id )
                return &entry;
        return nullptr;
    }

    // The quality-driven half of a component, lifted out. Compared before and after the Details block is
    // drawn to tell "the artist moved a performance knob" from "the artist moved a look knob".
    inline CloudQualityValues ExtractQualityValues( const ECS::VolumetricCloudData& data )
    {
        CloudQualityValues values;
#define DESERT_CLOUD_QUALITY_READ( Type, Name ) values.Name = data.Name;
        DESERT_CLOUD_QUALITY_FIELDS( DESERT_CLOUD_QUALITY_READ )
#undef DESERT_CLOUD_QUALITY_READ
        return values;
    }

    // Overwrites the thirteen performance knobs of @p data with the tier's. Pure, and - exactly like
    // ApplyPreset - it does NOT write data.QualityLevel: the caller records which tier it applied, so
    // "applying a tier leaves everything else alone" stays a claim a test can make.
    inline void ApplyQuality( ECS::CloudQuality id, ECS::VolumetricCloudData& data )
    {
        const CloudQualityEntry* entry = FindCloudQuality( id );
        if ( !entry )
            return;

#define DESERT_CLOUD_QUALITY_WRITE( Type, Name ) data.Name = entry->Values.Name;
        DESERT_CLOUD_QUALITY_FIELDS( DESERT_CLOUD_QUALITY_WRITE )
#undef DESERT_CLOUD_QUALITY_WRITE
    }

    // Which tier these settings ARE, or Custom when they are none of them. Look fields are not consulted,
    // so switching weather preset never silently renames the quality tier.
    inline ECS::CloudQuality MatchQuality( const ECS::VolumetricCloudData& data )
    {
        const CloudQualityValues values = ExtractQualityValues( data );
        for ( const CloudQualityEntry& entry : kCloudQualityTiers )
            if ( entry.Values == values )
                return entry.Id;
        return ECS::CloudQuality::Custom;
    }

    // Display name for a combo box. Custom has no row, so it is named here and nowhere else.
    inline const char* CloudQualityName( ECS::CloudQuality id )
    {
        const CloudQualityEntry* entry = FindCloudQuality( id );
        return entry ? entry->Name : "Custom";
    }
} // namespace Desert::Graphic
