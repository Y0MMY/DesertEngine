// The weather presets and the raymarch quality tiers, checked as the pure functions they are.
//
// Two claims carry the whole design and neither is provable by reading the table: that applying a look
// preset cannot move a performance knob, and that the preset NAME stored in the scene keeps telling the
// truth about the values next to it. Both are checkable on the CPU with no GPU in sight, which - since
// the editor cannot be launched in this environment - is the only kind of check available.
//
// The range assertions read the same generated reflection metadata the Details panel reads, so a preset
// value outside its field's declared slider, or a Range tightened later without revisiting the presets,
// fails here rather than as a slider that jumps the first time an artist touches it.

#include <Common/Core/Units.hpp>

#include <Engine/ECS/VolumetricCloudsComponent.hpp>
#include <Engine/Graphic/Clouds/CloudLayerAspect.hpp>
#include <Engine/Graphic/Clouds/CloudMarchScale.hpp>
#include <Engine/Graphic/Clouds/CloudWeatherScale.hpp>
#include <Engine/Graphic/CloudPresets.hpp>
#include <Engine/Graphic/CloudQuality.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionTypes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using Desert::ECS::CloudPreset;
using Desert::ECS::CloudQuality;
using Desert::ECS::CloudResolutionScale;
using Desert::ECS::CloudTemporalMode;
using Desert::ECS::VolumetricCloudData;
using Desert::Reflection::FieldInfo;
using Desert::Reflection::FieldType;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::TypeInfo;

namespace Graphic = Desert::Graphic;

namespace
{
    const TypeInfo& CloudType()
    {
        const TypeInfo* t = ReflectionRegistry::Get().Find( "VolumetricCloudData" );
        EXPECT_NE( t, nullptr ) << "VolumetricCloudData is not registered - the codegen did not run";
        return *t;
    }

    const FieldInfo* Field( const char* name )
    {
        const TypeInfo& type = CloudType();
        const auto      it   = std::find_if( type.Fields.begin(), type.Fields.end(),
                                             [name]( const FieldInfo& f ) { return f.Name == name; } );
        return it == type.Fields.end() ? nullptr : &*it;
    }

    // Everything on the component that a LOOK preset must never touch: the thirteen performance knobs,
    // the tier selector and the preset label itself.
    struct NonLookState
    {
        Graphic::CloudQualityValues Quality;
        CloudQuality                Level;
        CloudPreset                 Preset;
        bool                        Enabled;
        float                       MaxViewDistance;

        bool operator==( const NonLookState& ) const = default;
    };

    NonLookState CaptureNonLook( const VolumetricCloudData& d )
    {
        return NonLookState{ Graphic::ExtractQualityValues( d ), d.QualityLevel, d.Preset, d.Enabled,
                             d.MaxViewDistance };
    }

    // Everything a QUALITY tier must never touch.
    struct NonQualityState
    {
        Graphic::CloudPresetValues Look;
        CloudPreset                Preset;
        CloudQuality               Level;

        bool operator==( const NonQualityState& ) const = default;
    };

    NonQualityState CaptureNonQuality( const VolumetricCloudData& d )
    {
        return NonQualityState{ Graphic::ExtractPresetValues( d ), d.Preset, d.QualityLevel };
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// The table itself (CLD-50)
// ---------------------------------------------------------------------------------------------------

// A new enumerator without a row would not fail to compile - it would quietly select nothing. This is
// what turns that into a red test.
TEST( CloudPresets, EveryEnumeratorExceptCustomHasExactlyOneRow )
{
    const FieldInfo* preset = Field( "Preset" );
    ASSERT_NE( preset, nullptr );
    ASSERT_EQ( preset->Type, FieldType::Enum );

    // The reflected enumerator list is the authority: it is generated from the enum declaration, so the
    // count cannot drift from the C++ the way a hand-written list in this file could.
    EXPECT_EQ( std::size( Graphic::kCloudPresets ), preset->EnumValues.size() - 1u )
         << "kCloudPresets must have one row per CloudPreset enumerator except Custom";

    for ( const auto& enumerator : preset->EnumValues )
    {
        const auto  id     = static_cast<CloudPreset>( enumerator.Value );
        const auto* row    = Graphic::FindCloudPreset( id );
        const bool  custom = id == CloudPreset::Custom;
        EXPECT_EQ( row == nullptr, custom ) << enumerator.Name << " has the wrong number of table rows";
    }

    // Every row appears once, so a copy-pasted entry cannot shadow another preset.
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        const std::ptrdiff_t count =
             std::count_if( std::begin( Graphic::kCloudPresets ), std::end( Graphic::kCloudPresets ),
                            [&entry]( const Graphic::CloudPresetEntry& other ) { return other.Id == entry.Id; } );
        EXPECT_EQ( count, 1 ) << entry.Name << " appears more than once";
        EXPECT_NE( entry.Name, nullptr );
    }
}

// CLD-51: the component's own defaults ARE the Partly Cloudy row, so a freshly added component is a
// named weather rather than an anonymous one.
TEST( CloudPresets, ComponentDefaultsAreThePartlyCloudyRow )
{
    VolumetricCloudData applied{};
    Graphic::ApplyPreset( CloudPreset::PartlyCloudy, applied );

    const VolumetricCloudData fresh{};
    EXPECT_TRUE( Graphic::ExtractPresetValues( applied ) == Graphic::ExtractPresetValues( fresh ) );
    EXPECT_EQ( Graphic::MatchPreset( fresh ), CloudPreset::PartlyCloudy );
}

// ---------------------------------------------------------------------------------------------------
// Apply / Match (CLD-52)
// ---------------------------------------------------------------------------------------------------

TEST( CloudPresets, ApplyThenMatchRoundTripsForEveryPreset )
{
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( entry.Id, d );
        EXPECT_EQ( Graphic::MatchPreset( d ), entry.Id ) << entry.Name;
    }
}

TEST( CloudPresets, ApplyingTheSamePresetTwiceChangesNothingTheSecondTime )
{
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        VolumetricCloudData once{};
        Graphic::ApplyPreset( entry.Id, once );
        VolumetricCloudData twice = once;
        Graphic::ApplyPreset( entry.Id, twice );
        EXPECT_TRUE( Graphic::ExtractPresetValues( once ) == Graphic::ExtractPresetValues( twice ) ) << entry.Name;
    }
}

// Custom is a LABEL for hand-authored values, not a set of them: applying it must leave the component
// exactly as it was rather than resetting it to anything.
TEST( CloudPresets, ApplyingCustomLeavesEveryValueAlone )
{
    VolumetricCloudData d{};
    Graphic::ApplyPreset( CloudPreset::Storm, d );
    const Graphic::CloudPresetValues before = Graphic::ExtractPresetValues( d );

    Graphic::ApplyPreset( CloudPreset::Custom, d );
    EXPECT_TRUE( Graphic::ExtractPresetValues( d ) == before );
}

// The claim the whole "Preset" row rests on: perturbing any ONE preset-driven field must cost the name.
// The loop is over the reflection metadata, so a field added to CloudPresetValues without being reachable
// this way fails here instead of becoming a value the name silently stops describing.
TEST( CloudPresets, MovingAnySinglePresetDrivenFieldFallsBackToCustom )
{
    const TypeInfo& type = CloudType();

    std::size_t perturbed = 0;
    for ( const FieldInfo& f : type.Fields )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( CloudPreset::Stratus, d );

        // Is this field one of the 79? Ask the extractor rather than a list: write a marker into the
        // component, and see whether the extracted look values notice.
        const Graphic::CloudPresetValues before = Graphic::ExtractPresetValues( d );

        // RELATIVE, not "+ 0.125": these fields span 0.02 to six million (a distance in centimetres),
        // and at six million a float has no 0.125 to add — the perturbation silently vanishes and the
        // field looks untouched. That is the same precision trap the cloud shell maths has to respect,
        // met here at a much cheaper price.
        const auto bump = []( float v ) { return v * 1.5f + 1.0f; };

        auto* bytes = reinterpret_cast<std::byte*>( &d ) + f.Offset;
        switch ( f.Type )
        {
            case FieldType::Float:
            case FieldType::Vec3:
            case FieldType::Vec4: // moving the first component is enough to move the vector
            {
                float v = 0.0f;
                std::memcpy( &v, bytes, sizeof( v ) );
                v = bump( v );
                std::memcpy( bytes, &v, sizeof( v ) );
                break;
            }
            case FieldType::Int:
            {
                int v = 0;
                std::memcpy( &v, bytes, sizeof( v ) );
                v += 1;
                std::memcpy( bytes, &v, sizeof( v ) );
                break;
            }
            default:
                continue; // bools and the two selector enums are not preset-driven
        }

        if ( Graphic::ExtractPresetValues( d ) == before )
            continue; // not a preset-driven field - the quality group lands here, which is the point

        ++perturbed;
        EXPECT_EQ( Graphic::MatchPreset( d ), CloudPreset::Custom )
             << f.Name << " was edited and the preset name survived it";
    }

    // 85 since High Frequency Fade Start / End became the single High Frequency Feature Size.
    EXPECT_EQ( perturbed, 85u ) << "the preset drives a different number of fields than specified";
}

// The other half of the same claim: a QUALITY edit must not cost the weather name. This is why the
// revert is computed from the values rather than from "the artist touched something".
TEST( CloudPresets, MovingAQualityFieldLeavesThePresetNameIntact )
{
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( entry.Id, d );

        d.MaxSteps            = 41;
        d.MinStepSize         = Common::Units::Metres( 33.0f );
        d.TemporalMode        = CloudTemporalMode::Off;
        d.ResolutionScale     = CloudResolutionScale::Full;
        d.MultiScatterOctaves = 4;

        EXPECT_EQ( Graphic::MatchPreset( d ), entry.Id ) << entry.Name;
    }
}

// ---------------------------------------------------------------------------------------------------
// Presets do not reach quality, tiers do not reach look (CLD-53, CLD-60)
// ---------------------------------------------------------------------------------------------------

// CloudPresetValues makes this unable to compile wrong. The test guards the refactor that flattens the
// two structs back into one and quietly reintroduces the coupling.
TEST( CloudPresets, ApplyingAPresetTouchesNothingOutsideTheLookFields )
{
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        d.QualityLevel = CloudQuality::Ultra;
        Graphic::ApplyQuality( CloudQuality::Ultra, d );
        d.Preset          = CloudPreset::Cirrus;
        d.Enabled         = false;
        d.MaxViewDistance = Common::Units::Metres( 12345.0f );

        const NonLookState before = CaptureNonLook( d );
        Graphic::ApplyPreset( entry.Id, d );
        EXPECT_TRUE( CaptureNonLook( d ) == before ) << entry.Name << " moved something outside the look";
    }
}

TEST( CloudQualityTiers, ApplyingATierTouchesNothingOutsideTheThirteenKnobs )
{
    for ( const Graphic::CloudQualityEntry& entry : Graphic::kCloudQualityTiers )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( CloudPreset::Storm, d );
        d.Preset       = CloudPreset::Storm;
        d.QualityLevel = CloudQuality::Custom;

        const NonQualityState before = CaptureNonQuality( d );
        Graphic::ApplyQuality( entry.Id, d );
        EXPECT_TRUE( CaptureNonQuality( d ) == before ) << entry.Name << " moved a look field";
    }
}

// ---------------------------------------------------------------------------------------------------
// The quality table (CLD-60, CLD-61)
// ---------------------------------------------------------------------------------------------------

TEST( CloudQualityTiers, EveryEnumeratorExceptCustomHasExactlyOneRow )
{
    const FieldInfo* level = Field( "QualityLevel" );
    ASSERT_NE( level, nullptr );
    ASSERT_EQ( level->Type, FieldType::Enum );

    EXPECT_EQ( std::size( Graphic::kCloudQualityTiers ), level->EnumValues.size() - 1u );

    for ( const auto& enumerator : level->EnumValues )
    {
        const auto id     = static_cast<CloudQuality>( enumerator.Value );
        const bool custom = id == CloudQuality::Custom;
        EXPECT_EQ( Graphic::FindCloudQuality( id ) == nullptr, custom ) << enumerator.Name;
    }
}

TEST( CloudQualityTiers, ComponentDefaultsAreTheHighRow )
{
    VolumetricCloudData applied{};
    Graphic::ApplyQuality( CloudQuality::High, applied );

    const VolumetricCloudData fresh{};
    EXPECT_TRUE( Graphic::ExtractQualityValues( applied ) == Graphic::ExtractQualityValues( fresh ) );
    EXPECT_EQ( Graphic::MatchQuality( fresh ), CloudQuality::High );
}

TEST( CloudQualityTiers, ApplyThenMatchRoundTripsForEveryTier )
{
    for ( const Graphic::CloudQualityEntry& entry : Graphic::kCloudQualityTiers )
    {
        VolumetricCloudData d{};
        Graphic::ApplyQuality( entry.Id, d );
        EXPECT_EQ( Graphic::MatchQuality( d ), entry.Id ) << entry.Name;
    }
}

TEST( CloudQualityTiers, MovingAnySingleKnobFallsBackToCustom )
{
    VolumetricCloudData base{};
    Graphic::ApplyQuality( CloudQuality::Medium, base );

    const auto expectCustom = [&base]( auto&& mutate, const char* what )
    {
        VolumetricCloudData d = base;
        mutate( d );
        EXPECT_EQ( Graphic::MatchQuality( d ), CloudQuality::Custom ) << what;
    };

    expectCustom( []( VolumetricCloudData& d ) { d.ResolutionScale = CloudResolutionScale::Full; },
                  "ResolutionScale" );
    expectCustom( []( VolumetricCloudData& d ) { d.MaxSteps += 1; }, "MaxSteps" );
    expectCustom( []( VolumetricCloudData& d ) { d.MinStepSize += 1.0f; }, "MinStepSize" );
    expectCustom( []( VolumetricCloudData& d ) { d.MaxStepSize += 1.0f; }, "MaxStepSize" );
    expectCustom( []( VolumetricCloudData& d ) { d.StepGrowthRate += 0.001f; }, "StepGrowthRate" );
    expectCustom( []( VolumetricCloudData& d ) { d.CoarseStepMultiplier += 0.5f; }, "CoarseStepMultiplier" );
    expectCustom( []( VolumetricCloudData& d ) { d.EmptySamplesBeforeCoarse += 1; }, "EmptySamplesBeforeCoarse" );
    expectCustom( []( VolumetricCloudData& d ) { d.LightMarchSamples += 1; }, "LightMarchSamples" );
    expectCustom( []( VolumetricCloudData& d ) { d.MultiScatterOctaves += 1; }, "MultiScatterOctaves" );
    expectCustom( []( VolumetricCloudData& d ) { d.TemporalMode = CloudTemporalMode::Off; }, "TemporalMode" );
    expectCustom( []( VolumetricCloudData& d ) { d.TemporalBlendFactor += 0.01f; }, "TemporalBlendFactor" );
    expectCustom( []( VolumetricCloudData& d ) { d.TemporalClampScale += 0.1f; }, "TemporalClampScale" );
    expectCustom( []( VolumetricCloudData& d ) { d.JitterStrength -= 0.1f; }, "JitterStrength" );
}

// ---------------------------------------------------------------------------------------------------
// Every authored number lands inside the slider the artist will see (CLD-54)
// ---------------------------------------------------------------------------------------------------

TEST( CloudPresets, EveryPresetValueLiesInsideItsFieldsDeclaredRange )
{
    for ( const Graphic::CloudPresetEntry& entry : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( entry.Id, d );

        for ( const FieldInfo& f : CloudType().Fields )
        {
            if ( !f.Meta.HasRange )
                continue;

            const auto* bytes = reinterpret_cast<const std::byte*>( &d ) + f.Offset;
            if ( f.Type == FieldType::Float )
            {
                float v = 0.0f;
                std::memcpy( &v, bytes, sizeof( v ) );
                EXPECT_GE( v, f.Meta.RangeMin ) << entry.Name << '.' << f.Name;
                EXPECT_LE( v, f.Meta.RangeMax ) << entry.Name << '.' << f.Name;
            }
            else if ( f.Type == FieldType::Int )
            {
                int v = 0;
                std::memcpy( &v, bytes, sizeof( v ) );
                EXPECT_GE( static_cast<float>( v ), f.Meta.RangeMin ) << entry.Name << '.' << f.Name;
                EXPECT_LE( static_cast<float>( v ), f.Meta.RangeMax ) << entry.Name << '.' << f.Name;
            }
            else if ( f.Type == FieldType::Vec4 )
            {
                float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                std::memcpy( v, bytes, sizeof( v ) );
                for ( int i = 0; i < 4; ++i )
                {
                    EXPECT_GE( v[i], f.Meta.RangeMin ) << entry.Name << '.' << f.Name << '[' << i << ']';
                    EXPECT_LE( v[i], f.Meta.RangeMax ) << entry.Name << '.' << f.Name << '[' << i << ']';
                }
            }
        }
    }
}

TEST( CloudQualityTiers, EveryTierValueLiesInsideItsFieldsDeclaredRange )
{
    for ( const Graphic::CloudQualityEntry& entry : Graphic::kCloudQualityTiers )
    {
        VolumetricCloudData d{};
        Graphic::ApplyQuality( entry.Id, d );

        for ( const FieldInfo& f : CloudType().Fields )
        {
            if ( !f.Meta.HasRange || f.Meta.Category != "Quality" )
                continue;

            const auto* bytes = reinterpret_cast<const std::byte*>( &d ) + f.Offset;
            if ( f.Type == FieldType::Float )
            {
                float v = 0.0f;
                std::memcpy( &v, bytes, sizeof( v ) );
                EXPECT_GE( v, f.Meta.RangeMin ) << entry.Name << '.' << f.Name;
                EXPECT_LE( v, f.Meta.RangeMax ) << entry.Name << '.' << f.Name;
            }
            else if ( f.Type == FieldType::Int )
            {
                int v = 0;
                std::memcpy( &v, bytes, sizeof( v ) );
                EXPECT_GE( static_cast<float>( v ), f.Meta.RangeMin ) << entry.Name << '.' << f.Name;
                EXPECT_LE( static_cast<float>( v ), f.Meta.RangeMax ) << entry.Name << '.' << f.Name;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// Every preset x every tier is a usable configuration (CLD-63)
// ---------------------------------------------------------------------------------------------------

// The orderings below are the ones whose violation divides by zero in the raymarch: a fade whose end is
// before its start, a minimum step above the maximum, a height gradient that runs backwards. Checking
// them across all 7 x 4 combinations is cheap and catches a preset authored against the wrong tier.
TEST( CloudPresets, EveryPresetTimesEveryTierSatisfiesTheOrderingInvariants )
{
    const auto nonDecreasing = []( const glm::vec4& g ) { return g.x <= g.y && g.y <= g.z && g.z <= g.w; };

    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        for ( const Graphic::CloudQualityEntry& tier : Graphic::kCloudQualityTiers )
        {
            VolumetricCloudData d{};
            Graphic::ApplyPreset( preset.Id, d );
            Graphic::ApplyQuality( tier.Id, d );

            const std::string where = std::string( preset.Name ) + " / " + tier.Name;

            EXPECT_LE( d.MinStepSize, d.MaxStepSize ) << where;
            EXPECT_LE( d.HorizonFadeStart, d.HorizonFadeEnd ) << where;
            EXPECT_LE( d.NearFadeStart, d.NearFadeEnd ) << where;
            EXPECT_LE( d.SofteningStartDistance, d.SofteningEndDistance ) << where;
            EXPECT_LE( d.DistanceFadeStart, d.DistanceFadeEnd ) << where;
            EXPECT_GT( d.HighFreqFeatureSize, 0.0f ) << where;

            EXPECT_TRUE( nonDecreasing( d.StratusGradient ) ) << where;
            EXPECT_TRUE( nonDecreasing( d.StratocumulusGradient ) ) << where;
            EXPECT_TRUE( nonDecreasing( d.CumulusGradient ) ) << where;
            // The three forms the Cloud Type axis gained. A gradient whose base-in ends after its
            // top-out begins is a profile that never opens, i.e. a type nobody can see.
            EXPECT_TRUE( nonDecreasing( d.ShelfGradient ) ) << where;
            EXPECT_TRUE( nonDecreasing( d.CongestusGradient ) ) << where;
            EXPECT_TRUE( nonDecreasing( d.AnvilGradient ) ) << where;

            // A layer with no thickness and a march with no steps are both a division waiting to happen.
            EXPECT_GT( d.LayerThickness, 0.0f ) << where;
            EXPECT_GT( d.MaxSteps, 0 ) << where;
            EXPECT_GT( d.ExtinctionScale, 0.0f ) << where;
        }
    }
}

// Two values that must agree and never did: the weather field's horizontal scale and the layer's
// altitude. Clouds_UEShowcase authored a 60 km tile over a layer at 1.5-5 km, so a ground camera saw one
// coverage cell overhead and the zenith rendered as empty blue while the horizon carried a dense band.
// Nothing related the two, so every preset drifted independently — the Overcast row was a 120 km tile
// over a layer at 0.9-3.1 km, seven times what its own altitude asks for.
//
// The relation is CloudAutoWeatherTileSize (Engine/Graphic/Clouds/CloudWeatherScale.hpp, and the same
// formula in Common/CloudGeometry.glslh with the reasoning). A preset that leaves the plausible band is
// a sky an artist will report as broken, so it fails here instead.
TEST( CloudPresets, EveryPresetsWeatherTileMatchesItsOwnLayerAltitude )
{
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        // NO ROW IS EXEMPTED, BY NAME OR BY CLAUSE. All eight sit inside the band, and after the lift
        // SEVEN of them sit at exactly 1.000 because they are solved to the derivation. Only Cirrus is
        // still authored, at 1.333 — its tile was the derived tile at the three cells overhead the
        // constant used to carry. The band is [0.9333, 2.1333] and it is derived from
        // kCloudWeatherCellsOverhead rather than written down, so a future change to the count moves
        // every one of these ratios and the band together.
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );

        const float wanted = Graphic::CloudAutoWeatherTileSize( d.LayerBottomAltitude, d.LayerThickness );
        EXPECT_TRUE(
             Graphic::CloudWeatherTileIsPlausible( d.WeatherTileSize, d.LayerBottomAltitude, d.LayerThickness ) )
             << preset.Name << ": tile " << Common::Units::ToMetres( d.WeatherTileSize ) / 1000.0f
             << " km over a layer at " << Common::Units::ToMetres( d.LayerBottomAltitude ) / 1000.0f << "-"
             << Common::Units::ToMetres( d.LayerBottomAltitude + d.LayerThickness ) / 1000.0f << " km wants "
             << Common::Units::ToMetres( wanted ) / 1000.0f << " km";
    }
}

// THE OTHER PAIR THAT MUST AGREE, and the one that made a deck overhead read as a ceiling: the layer's
// thickness against the coverage cell it lives under.
//
// Both numbers were individually defensible — a 3.5 km deck is a real depth and a 23.8 km weather tile is
// what that altitude asks for — and their RATIO was 0.85, i.e. a cloud taller than it was wide. Those are
// cumulonimbus proportions, and the whole fair-weather family carried them: Partly Cloudy 0.85, Summer
// Cumulus 0.78, Overcast 0.83, Fair Weather 1.01. Nothing in the engine said a cloud should be wider than
// it is tall, so nothing noticed. See Engine/Graphic/Clouds/CloudLayerAspect.hpp.
TEST( CloudPresets, EveryPresetsThicknessRealisesItsSpeciesAspect )
{
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );

        const float realised = Graphic::CloudLayerAspect( d.WeatherTileSize, d.LayerThickness );

        // The row's thickness IS the derived one: the aspect it actually realises is the aspect it claims.
        EXPECT_NEAR( realised, preset.TargetAspect, 1e-3f )
             << preset.Name << ": " << Common::Units::ToMetres( d.LayerThickness ) / 1000.0f
             << " km thick under a " << Common::Units::ToMetres( d.WeatherTileSize ) / 8000.0f << " km cell";

        // And the aspect it claims is one its species can actually have. A species is a range rather than a
        // number — real cumulus humilis run 1.5 to 3 times wider than deep — so this is the assertion that
        // the row's single number is a member of it, not that the atmosphere has one answer.
        EXPECT_TRUE( Graphic::CloudAspectSuitsSpecies( preset.TargetAspect, preset.Species ) )
             << preset.Name << " claims to be " << preset.Species.Name << " at " << preset.TargetAspect
             << ", outside [" << preset.Species.Low << ", " << preset.Species.High << "]";
    }
}

// A BOUND, not a target, and the difference is the point of having both.
//
// The species targets above are intent: a preset that misses its species by a little is a look note. This
// is the part that is not a matter of taste — a cloud narrower than it is tall is a convective tower, and
// a tower that is not deep is a slab standing on end. It is the same predicate the renderer warns from,
// so a preset can never ship a geometry the engine would complain about at runtime.
TEST( CloudPresets, NoPresetIsTallerThanItIsWideWithoutTheDepthToBeACumulonimbus )
{
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );

        EXPECT_TRUE( Graphic::CloudLayerAspectIsPlausible( d.WeatherTileSize, d.LayerThickness ) )
             << preset.Name << ": aspect " << Graphic::CloudLayerAspect( d.WeatherTileSize, d.LayerThickness )
             << " at " << Common::Units::ToMetres( d.LayerThickness ) / 1000.0f << " km thick";
    }

    // Storm is the exemption and it is asserted as one: it IS taller than it is wide, and what earns it is
    // the 9 km depth. Drop that depth and the same proportions stop being a cumulonimbus.
    VolumetricCloudData storm{};
    Graphic::ApplyPreset( CloudPreset::Storm, storm );
    EXPECT_LT( Graphic::CloudLayerAspect( storm.WeatherTileSize, storm.LayerThickness ), 1.0f );
    EXPECT_GE( storm.LayerThickness, Graphic::kCloudDeepConvectionThickness );

    // Take the depth away and the same weather stops being allowed to be taller than wide.
    //
    // THE WINDOW NARROWED WHEN STORM WAS LIFTED, and the shallow thickness is written against the
    // threshold rather than as a literal because of it. The probe has to be taller than Storm's coverage
    // cell (or the aspect is not below one and there is nothing to disallow) and shorter than
    // kCloudDeepConvectionThickness (or the depth excuses it). Storm's cell went 4762 m -> 5874.3 m with
    // the lift, so the window went [4762, 6000) to [5874.3, 6000): one metre under the threshold is the
    // only expression of "shallow" that stays inside it without being re-tuned every time the row moves.
    // At 5999 m under a 5874.3 m cell the aspect is 0.979.
    const float shallow = Graphic::kCloudDeepConvectionThickness - Common::Units::Metres( 1.0f );
    ASSERT_LT( shallow, Graphic::kCloudDeepConvectionThickness );
    ASSERT_GT( shallow, storm.WeatherTileSize / Graphic::kCloudWeatherBasePeriod )
         << "the probe is shorter than Storm's own coverage cell, so its aspect is not below one and this "
            "row is asserting nothing";
    EXPECT_LT( Graphic::CloudLayerAspect( storm.WeatherTileSize, shallow ), 1.0f );
    EXPECT_FALSE( Graphic::CloudLayerAspectIsPlausible( storm.WeatherTileSize, shallow ) );
}

// THE INVERSE IS THE INVERSE, over the whole authored range. CloudLayerThicknessForAspect is what the
// preset table's thicknesses are derived from and what the renderer quotes in its warning; if it and
// CloudLayerAspect ever disagree, every derived thickness is wrong by the same silent factor.
TEST( CloudPresets, ThicknessForAspectInvertsTheAspect )
{
    // The shipped tiles, smallest to largest, plus a 1 km one below anything authored. Partly Cloudy's
    // is 5398060 since the lift took the cumulus rows to the 8 km band; this list is here to exercise the
    // inverse over the range the table actually occupies, so it has to follow the table.
    for ( float tile : { 100000.0f, 2277840.0f, 2613730.0f, 5398060.0f, 6300880.0f } )
        for ( float aspect : { 0.4f, 0.529f, 1.0f, 1.3f, 1.85f, 6.563f, 12.0f } )
        {
            const float thickness = Graphic::CloudLayerThicknessForAspect( tile, aspect );
            EXPECT_NEAR( Graphic::CloudLayerAspect( tile, thickness ), aspect, aspect * 1e-4f )
                 << "tile " << tile << " aspect " << aspect;
        }

    // MONOTONE, and strictly: a thicker layer under the same weather is a narrower cloud. This is what
    // makes "raise the aspect" and "thin the layer" the same instruction, which is how the presets were
    // re-authored and how the warning's advice is phrased.
    const float tile     = 5398060.0f;
    float       previous = Graphic::CloudLayerAspect( tile, 50000.0f );
    for ( float thickness = 60000.0f; thickness <= 1500000.0f; thickness += 10000.0f )
    {
        const float aspect = Graphic::CloudLayerAspect( tile, thickness );
        EXPECT_LT( aspect, previous ) << "thickness " << thickness;
        previous = aspect;
    }
}

// THE THIRD RELATION IS STILL SATISFIED AFTER THE SECOND MOVED, which is the interaction the aspect pass
// had to be checked against rather than assumed past: thinning a layer to widen its clouds takes it
// TOWARD CloudMarchScale's search bound, and the Low tier's 60 m minimum step is what caps how far the
// re-authoring could go. Every preset clears four samples on every shipped tier — that is what decided
// Fair Weather at 1.70 rather than 2.0 and Overcast at 1.30 rather than the broader sheet.
//
// AND IT IS THE TEST THAT SAYS THE ALTITUDE MOVES WERE HONEST. Buying angular size by moving the layer
// instead of by shrinking the cloud is the whole claim, and the way to tell the two apart is right here:
// solved at the OLD 1.5 km base, the same aspect gives Partly Cloudy a 683.9 m layer and 1.99 samples at
// Low — under half the bound, a march that steps over its own deck depending on the ray's dither phase.
// Solved at 8 km it is 3647.3 m and 4.15. Measured on the shipped rows at Low, after the lift: Clear 4.26,
// Fair Weather 4.52, Partly Cloudy 4.15, Summer Cumulus 6.39, Stratus 5.81, Overcast 5.92, Storm 14.49.
//
// STRATUS IS NO LONGER SKIPPED. It used to be, because at a 600 m base it was a 700 m sheet that failed at
// Low and Medium and nobody had lifted it. The lift derived it at 3000 m, where it clears every tier with
// margin, so the exemption is gone rather than carried — and if a future pass puts it back under the bound
// this row goes red instead of quietly skipping it.
TEST( CloudPresets, EveryPresetStillClearsTheSearchBoundOnEveryTierAfterTheAspectPass )
{
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );

        // Cirrus is the one shipped thin sheet left, and it is documented as failing the bound at the two
        // cheap tiers (CloudMath's ADeckAndAThinSheetBothGetEnoughSearchSamples..., and the note at the
        // Cirrus row itself: deriving it costs High 4.06 -> 3.05 at ANY altitude). It is deliberately not
        // lifted, so it is not this test's business; what matters is that nothing the lift touched JOINED
        // it.
        if ( preset.Id == CloudPreset::Cirrus )
            continue;

        for ( const auto& tier : Graphic::kCloudQualityTiers )
        {
            const Graphic::CloudSearchAcrossLayer worst = Graphic::CloudWorstSearchAcrossLayer(
                 d.LayerBottomAltitude, d.LayerThickness, tier.Values.MinStepSize, tier.Values.MaxStepSize,
                 tier.Values.StepGrowthRate, tier.Values.CoarseStepMultiplier );
            EXPECT_GE( worst.Samples, Graphic::kCloudMinSearchSamplesAcrossLayer )
                 << preset.Name << " on " << tier.Name << " at " << worst.ElevationDegrees << " degrees";
        }
    }
}

// D7 OF THE DECK-SCALE DECISION, ASSERTED RATHER THAN LEFT AS AN ABSENCE. SEVEN of the eight rows are now
// the simultaneous solution of CloudWeatherScale and CloudLayerAspect at their own base altitude; CIRRUS is
// the one that is deliberately NOT, and the next person to read this table has to be able to tell "decided
// against" from "nobody got round to".
//
// WHAT CHANGED, AND WHY THE OLD VERSION OF THIS TEST NAMED FOUR ROWS. Stratus, Overcast and Storm used to
// be exempt too, and every one of those exemptions was a failure of the ALTITUDE rather than of the
// derivation. At their old bases — 600 m, 900 m, 700 m — solving the pair gave Stratus 458.1 m under a
// 4555.7 m tile (below Weather Tile Size's own 5000 m field minimum) and 1.50 search samples at Low,
// Overcast 646.3 m and 2.00 samples, and Storm 2591.0 m of depth against kCloudDeepConvectionThickness's
// 6 km, i.e. a cumulonimbus no longer deep enough to be one. Lifting them to 3000 / 3500 / 3000 m makes all
// three solutions affordable — 22778.4 / 26137.3 / 46994.2 m of tile and 5.81 / 5.92 / 14.49 samples at Low
// — so the refusals expire with the altitude that caused them and the rows are derived like the rest.
//
// WHY CIRRUS IS NOT, and it is the one refusal that does NOT expire with altitude: solving the pair at its
// 6.563 aspect thins the sheet to 883.5 m and takes CloudMarchScale's worst search at the High tier from
// 4.06 to 3.05, through a bound of four with no tolerance in it. The solved thickness is proportional to
// the base at fixed aspect, so that 3.05 is scale-free — 3.05 at 8 km, 3.05 at 10 km, 3.05 at 11 km.
// There is no altitude that buys it, and Cirrus is already the highest row in the table anyway.
//
// The assertions below are written against the ratio a DERIVED row realises rather than against the raw
// ratio, because the raw one is a function of kCloudWeatherCellsOverhead and this statement is not.
TEST( CloudPresets, OnlyCirrusIsDeliberatelyNotDerived )
{
    // The yardstick. A row the derivation produced sits at exactly its own derived tile — 1.000x at four
    // cells overhead, 0.75x at three — because its tile and its thickness were solved together against
    // whatever that constant is. Everything below is measured in units of THAT.
    const auto tileRatio = []( CloudPreset id )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( id, d );
        return d.WeatherTileSize / Graphic::CloudAutoWeatherTileSize( d.LayerBottomAltitude, d.LayerThickness );
    };
    const float derivedRow = tileRatio( CloudPreset::PartlyCloudy );
    ASSERT_NEAR( derivedRow, 1.0f, 1e-3f );

    // EVERY ROW EXCEPT CIRRUS IS AT THE DERIVATION. This is the part that used to be an absence: "the four
    // cumulus rows are solved" was asserted only of those four, so a sheet row drifting off its own
    // derivation had nothing to fail against. Now the statement is about the whole table minus one.
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        if ( preset.Id == CloudPreset::Cirrus )
            continue;

        EXPECT_NEAR( tileRatio( preset.Id ), 1.0f, 1e-3f )
             << preset.Name << " is no longer at its own derived tile";
    }

    // Cirrus's authored geometry, pinned. "Not derived" is a claim about these three numbers, so a future
    // pass that quietly solves it fails here rather than passing as a rounding change.
    VolumetricCloudData cirrus{};
    Graphic::ApplyPreset( CloudPreset::Cirrus, cirrus );

    EXPECT_FLOAT_EQ( cirrus.LayerBottomAltitude, Common::Units::Metres( 8000.0f ) );
    EXPECT_FLOAT_EQ( cirrus.LayerThickness, Common::Units::Metres( 1200.0f ) );
    EXPECT_FLOAT_EQ( cirrus.WeatherTileSize, Common::Units::Metres( 63008.8f ) );

    // AND IT IS ADMISSIBLE AS AUTHORED, with no clause and no by-name exemption anywhere: its tile was the
    // derived tile at the three cells overhead the constant used to carry, so it sits at 1.333x today, and
    // the band is [0.9333, 2.1333] derived from that same constant (CloudWeatherScale.hpp).
    EXPECT_TRUE( Graphic::CloudWeatherTileIsPlausible( cirrus.WeatherTileSize, cirrus.LayerBottomAltitude,
                                                       cirrus.LayerThickness ) )
         << "Cirrus is no longer admissible as authored";
    EXPECT_NEAR( tileRatio( CloudPreset::Cirrus ), 4.0f / 3.0f, 1e-3f );

    // AND CIRRUS IS THE HIGHEST ROW, which is the first half of why it is not lifted. Asserted so that a
    // pass which raises another row above it has to come back and re-read the refusal at the Cirrus row.
    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );
        EXPECT_LE( d.LayerBottomAltitude, cirrus.LayerBottomAltitude )
             << preset.Name << " now sits above Cirrus; the refusal to lift Cirrus assumed it was the top row";
    }
}

// EVERY SPECIES KEEPS ITS BASE ON A CONDENSATION LEVEL — except the one whose base is supposed to be a
// mess, and that exception is asserted too rather than merely allowed.
//
// The base-in width of a profile row is the height over which a cloud goes from clear air to full body,
// and it is the gain that turns horizontal coverage variation into VERTICAL scatter of the cloud bottom
// (CloudMath asserts that proportionality on the table itself). A cumulus, a stratocumulus, a stratus
// sheet and a congestus tower all condense at the same kind of thermodynamic surface, so all four get a
// few hundredths; a cumulonimbus base really is lowered, ragged with scud and streaked with rain, so the
// anvil row gets the widest ramp in the table and is REQUIRED to.
//
// The bounds carry margin over the authored values (0.05-0.07 against 0.06-0.08) so that a deliberate
// retune passes and a regression to the pre-2026-08 widths — 0.22 for cumulus, 0.45 for congestus —
// fails. Those widths were 650 m and 1.3 km of ramp on the shipped decks, and they are the larger half of
// "the clouds look like they are close to the ground, not up in the sky".
TEST( CloudPresets, EveryPresetKeepsItsCloudBasesOnOneCondensationLevel )
{
    const auto check = []( const char* who, const VolumetricCloudData& d )
    {
        EXPECT_LE( d.StratusGradient.y, 0.06f ) << who << ": a sheet sits ON its condensation level";
        EXPECT_LE( d.StratocumulusGradient.y, 0.07f ) << who << ": a cellular deck shares one level";
        EXPECT_LE( d.ShelfGradient.y - d.ShelfGradient.x, 0.06f ) << who << ": a shelf IS its hard flat base";
        EXPECT_LE( d.CumulusGradient.y, 0.06f ) << who << ": fair-weather cumulus have the sharpest bases";
        EXPECT_LE( d.CongestusGradient.y, 0.08f ) << who << ": a tower spends its extra depth upward";

        // The deliberate exception, and the widest ramp in the table by construction.
        EXPECT_GE( d.AnvilGradient.y, 0.12f ) << who << ": a storm base is lowered and diffuse, not sharp";
        EXPECT_GT( d.AnvilGradient.y, d.CumulusGradient.y )
             << who << ": the anvil has stopped being the soft-based form";
        EXPECT_GT( d.AnvilGradient.y, d.CongestusGradient.y ) << who;
    };

    check( "component defaults", VolumetricCloudData{} );

    for ( const Graphic::CloudPresetEntry& preset : Graphic::kCloudPresets )
    {
        VolumetricCloudData d{};
        Graphic::ApplyPreset( preset.Id, d );
        check( preset.Name, d );
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
