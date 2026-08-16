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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
