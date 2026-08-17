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
    //
    // ---- WHAT PINS THE FIVE MARCH NUMBERS ------------------------------------------------------------
    //
    // MaxSteps, MinStepSize, StepGrowthRate, CoarseStepMultiplier and EmptySamplesBeforeCoarse are not
    // free. Each is bounded by a relation that lives somewhere else, and the bounds now hold against a
    // DIFFERENT march than the one they were first tuned for: the fine tier used to be entered and
    // abandoned constantly (it judged occupancy by the ERODED density while the coarse tier judged the
    // profile), and every ray used to lose a dithered slab off the near face of its layer. Both are
    // fixed, so the numbers were re-derived rather than inherited. The three relations, and what each
    // leaves:
    //
    //   R1  THE NYQUIST GATE (CloudNyquistWeight, Common/CloudGeometry.glslh). A march samples the medium
    //       at S, so a feature of size F is fully carried at S <= F/4 and gone at S >= F/2 - and the gate
    //       reads the step itself, so nothing ever aliases; detail simply STOPS. MinStepSize and
    //       StepGrowthRate therefore decide HOW FAR the erosion's detail survives, which is a look
    //       decision wearing a performance knob's name. On the shipped 4 km detail tile the coarse
    //       channel pair (781 m) is carried in full to 14.4 km on Low, 19.6 on Medium, 24.0 on High and
    //       30.1 on Ultra. Growing either knob to buy frame time moves that distance in, visibly.
    //
    //   R2  THE PROFILE-OCCUPANCY RULE (CloudMarchAdvance's caller). Both tiers judge by the profile, so a
    //       fine excursion now runs a whole cloud instead of ending on every erosion hole. That is what
    //       MaxSteps is FOR now: marching, not funding re-entries. It also means the budget is BINDING
    //       again - crossing the whole deck chord at the shipped horizon view (6.8 degrees) needs 190 fine
    //       steps against High's 176. Measured: dropping High to 144 costs 5-6 grey levels RMS and 0.43%
    //       to 0.48% of the frame's mean luminance at all three elevations. The deck gets thinner. Not
    //       available.
    //
    //   R3  THE SEARCH BOUND (CloudMarchScale.hpp, >= 4 samples across the thinnest layer at its worst
    //       elevation). This is what pins CoarseStepMultiplier, and it pins it HARD: on the Cirrus
    //       preset's 1.2 km sheet the High row gets 4.06 samples at 20 degrees, and the largest multiplier
    //       that still clears the bound is 4.05. There is 1.3% of headroom in the number and the CloudMath
    //       suite fails if it is spent. It would not be worth spending in any case — a wider coarse stride
    //       skips empty sky faster but pays a one-coarse-stride rewind at every cloud it finds, and the
    //       rewind wins: 8.0 measured +3.7% at the zenith and +3.0% at the horizon.
    //
    // EmptySamplesBeforeCoarse has no closed-form bound, so it was measured against the machine that now
    // exists. Leaving the fine tier after E empty samples costs E strides of overshoot past every cloud;
    // re-entering costs the one-coarse-stride rewind, so exiting only pays for a gap longer than about
    // (E + c) * c / (c - 1) fine strides. Under the OLD rule the gaps were erosion holes - short, dense
    // and placed by the dither - and under the new one they are real breaks in coverage, so the optimum
    // had every reason to move. It did not: 8 is a local minimum, with 5 and 12 both measuring +2.7% at
    // the horizon (and the earlier 8 -> 2 trial, which was worse still, was never a candidate - it puts E
    // below CoarseStepMultiplier, where an exit costs more ground than it saves).
    //
    // MaxStepSize is the one number with room in it — it does not bite until 85.6 km on High, and the
    // R3 minimum sits at 20 degrees where the clamp never applies, so it can be raised without touching
    // any bound. It is also not worth raising: 1400 m measured +0.5% at the horizon, because the rays it
    // reaches are budget-exhausted either way. It is a quality knob for grazing rays, not a saving.
    //
    // SO THE SCHEDULE IS PINNED ON EVERY SIDE, and the frame time recovered in this pass came from the
    // layer-count specialization constant instead (CloudPayload.hpp, Programs/Clouds/CloudRaymarch.shader)
    // — 11% to 18% of a one-layer frame, for a picture that is identical to the bit.

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
         // LOW IS A DECK TIER AND NOTHING ELSE. R3 gives it 1.56 search samples across the Cirrus
         // preset's 1.2 km sheet — it can stride over a thin high layer entirely, and the renderer says
         // so out loud when a scene asks it to. R1 keeps its detail to 14.4 km. What it is FOR is a low
         // cumulus deck on a machine that cannot afford the shell, at quarter resolution with no temporal
         // stage to lean on.
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
         // MEDIUM IS THE SAME DECK TIER WITH THE TEMPORAL STAGE UNDER IT. Half resolution plus
         // reprojection is what buys the apparent detail its 30 m step cannot resolve on its own. It is
         // STILL under R3's bar for a thin sheet (3.53 samples), and deliberately: clearing it would cost
         // the coarse multiplier, which is most of what makes this tier cheaper than High.
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
         // HIGH IS THE SHIPPED TIER, AND IT IS THE CHEAPEST ROW THAT CLEARS EVERY RELATION AT ONCE: R3's
         // four search samples on the thinnest layer any preset authors (4.06, against a ceiling of 4.05
         // on the coarse multiplier), R1's detail out to 24.0 km, and R2's budget across the deck chord at
         // an ordinary viewing elevation. Every one of the five numbers below sits against a wall; none of
         // them has slack to give back. See the derivation at the top of this file.
         { ECS::CloudQuality::High, "High",
           CloudQualityValues{
                .ResolutionScale = ECS::CloudResolutionScale::Half,
                // 176, up from 128: the sqrt-type near schedule (CloudStepLength) samples the first
                // 10 km at <= 2x MinStepSize, ~1.8x the steps the old linear schedule spent there — and
                // full-strength erosion thins the media, so the transmittance early-out fires later. At
                // 144 the showcase's near tower ran out of budget mid-cloud and the exhaustion fade
                // dissolved it; 176 keeps it. Coarse 4, up from 3, pays for part of it: the coarse
                // stride follows the fine one, so the near schedule had tripled the cost of skipping
                // EMPTY sky; 4 x 30 m at 10 km is still finer than the old 3 x 95 m. Both measured by
                // frame-count slope against the pre-schedule baseline, inside the 1.5x gate.
                //
                // RE-DERIVED against the profile-occupancy rule (R2) rather than inherited from the march
                // that thrashed: the fine tier now runs a whole cloud without handing control back, so
                // this budget buys integration instead of re-entries — and it is still short. Crossing the
                // deck's own chord at 6.8 degrees needs 190 of these steps; 144 was rendered and measured
                // at 5.0-6.2 grey levels RMS below 176 with the frame's mean luminance down 0.43-0.48%,
                // which is the deck getting thinner. It stays at 176.
                .MaxSteps       = 176,
                .MinStepSize    = Common::Units::Metres( 15.0f ),
                .MaxStepSize    = Common::Units::Metres( 700.0f ),
                .StepGrowthRate = 0.008f,
                // 4.0, and 4.05 is the ceiling: R3 gives the Cirrus preset's 1.2 km sheet 4.06 search
                // samples at 20 degrees with this multiplier, and the bound is 4. There is nothing here
                // to spend, and CloudMath's ADeckAndAThinSheetBothGetEnoughSearchSamplesAtTheirAuthoredTiers
                // is what stops the next pass spending it anyway.
                .CoarseStepMultiplier = 4.0f,
                // 8 is a MEASURED local minimum on the machine the profile-occupancy rule produced, not a
                // number carried over from the one before it: 5 and 12 both cost +2.7% at the horizon.
                // Below CoarseStepMultiplier it is not a candidate at all — an exit that costs a
                // one-coarse-stride rewind cannot pay for itself in fewer strides than that rewind.
                .EmptySamplesBeforeCoarse = 8,
                // 4, not 6. The cone march is 12 of the 18 texture fetches a shaded sample costs — two
                // per cone sample — so this one number is two thirds of the raymarch. Four keeps the
                // shadow terminator readable; Ultra below still authors 5 for stills and captures.
                .LightMarchSamples = 4,
                // 3 octaves since CLD-108: at tauSun >~ 3 two octaves both vanish and a storm interior
                // collapses onto ambient alone. The third costs arithmetic, not fetches.
                .MultiScatterOctaves = 3,
                .TemporalMode        = ECS::CloudTemporalMode::Reprojection,
                .TemporalBlendFactor = 0.10f,
                .TemporalClampScale  = 1.50f,
                .JitterStrength      = 1.00f,
           } },
         // ULTRA IS FOR STILLS AND CAPTURES, and what it buys over High is spelled out by the same three
         // relations: R1 carries the detail to 30.1 km instead of 24.0, R3 gives the thin sheet 7.05
         // search samples instead of 4.06 (so it has real margin where High has 1.3%), and full
         // resolution plus a sixth cone sample resolve what the temporal stage reconstructs below it.
         // What it does NOT buy is budget: R2 leaves it shorter than High relative to its own finer
         // schedule (235 fine steps needed at 6.8 degrees against 192), which is the honest cost of a
         // finer stride over the same chord.
         { ECS::CloudQuality::Ultra, "Ultra",
           CloudQualityValues{
                .ResolutionScale = ECS::CloudResolutionScale::Full,
                // 192/15 m/3, from 192/12 m/2, retuned for the sqrt-type near schedule. On a cloudy
                // horizon most full-res rays are budget-capped, so Ultra's slope tracks
                // MaxSteps x (shaded fraction) — and the near schedule raises the shaded fraction. At
                // 12 m the fine tier alone blew the 1.25x slope gate (measured 1.3-1.6x across
                // windows); 15 m authors the same <= 30 m bound out to 10 km that High does, and Ultra
                // keeps its edge where it actually shows at full resolution: the pixel count, the 6
                // cone samples, the finer far growth and the calmer temporal. MaxSteps stays the old
                // row's 192 — the sqrt schedule REDISTRIBUTES that budget toward the near field rather
                // than growing it; coarse 3 keeps the empty-sky skip at 3 x 30 m = 90 m out to 10 km,
                // finer than the old 2 x (12 m + 0.006 t) everywhere beyond ~3 km.
                .MaxSteps                 = 192,
                .MinStepSize              = Common::Units::Metres( 15.0f ),
                .MaxStepSize              = Common::Units::Metres( 500.0f ),
                .StepGrowthRate           = 0.006f,
                .CoarseStepMultiplier     = 3.0f,
                .EmptySamplesBeforeCoarse = 8,
                // 5, down from 6: two fetches per cone sample, paid on every shaded sample beyond the
                // shadow map's extent — the far half of the shell on this scene. The near schedule
                // multiplies shaded samples, so the sixth cone sample is what pushed Ultra past its
                // 1.25x slope gate after the MaxSteps and MinStepSize trades; still one more than High.
                .LightMarchSamples   = 5,
                .MultiScatterOctaves = 3,
                .TemporalMode        = ECS::CloudTemporalMode::Reprojection,
                .TemporalBlendFactor = 0.08f,
                .TemporalClampScale  = 1.75f,
                .JitterStrength      = 0.50f,
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
