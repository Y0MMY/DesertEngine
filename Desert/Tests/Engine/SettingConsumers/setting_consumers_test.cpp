// Does every exposed setting actually reach something?
//
// A slider that moves nothing is the failure mode this programme was written to avoid, and it is not
// caught by any build: an unread field compiles, serializes, appears in Details and does nothing at all.
// So the reflected field list of both sky and cloud components is enumerated here and matched against an
// explicit table that says, for every single field, WHO consumes it.
//
// The table has two kinds of row:
//
//   * WIRED   - names a source file that must exist and must mention the field. If someone deletes the
//               read while leaving the field, that file stops mentioning it and this test goes red.
//   * PENDING - names the TASK that owes the field a consumer. The cloud component is fully authored but
//               its render passes are still being written, so most of its 95 fields legitimately have no
//               reader yet. Writing that down per field is the difference between "not built yet" and
//               "forgotten": a field nobody ever claims is visible here as a field with no owner.
//
// Every reflected field must appear in exactly one row, so a field added tomorrow fails this test until
// somebody decides which of the two it is. That decision is the point.

#include <Engine/ECS/Components.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionTypes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Desert::Reflection::FieldInfo;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::TypeInfo;

namespace
{
    struct Row
    {
        const char* Field;

        // Exactly one of these is set. Where = a repo-relative source path; Task = the task that owes a
        // consumer, with what it will do with the value.
        const char* Where = nullptr;
        const char* Task  = nullptr;
    };

    // ------------------------------------------------------------------------------------------------
    // Sky: every artistic-gradient field is wired; the physical-atmosphere group is wired through the
    // LUT passes except the five fields the Phase 2/3 integrations will read (each names its phase).
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kSkySettings = "Desert/Desert/Source/Engine/Graphic/SkySettings.hpp";
    constexpr const char* kTimeOfDay   = "Desert/Desert/Source/Engine/ECS/System/TimeOfDayECSSystem.hpp";
    constexpr const char* kCollector   = "Desert/Desert/Source/Engine/ECS/System/SkyboxECSSystem.hpp";
    constexpr const char* kSkyWidget =
         "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/SkyAtmosphereComponent.cpp";

    constexpr Row kSkyRows[] = {
         // The collector decides whether the atmosphere drives the frame at all.
         { "Enabled", kCollector },

         // MakeSkySettings is the one funnel from component to render command; from there the palette
         // reaches the screen pass and the IBL bake through the same packed block.
         { "SkyBrightness", kSkySettings },
         { "HorizonFalloff", kSkySettings },
         { "ZenithColor", kSkySettings },
         { "HorizonColor", kSkySettings },
         { "GroundColor", kSkySettings },
         { "NightColor", kSkySettings },
         { "SunIntensity", kSkySettings },
         { "SunColor", kSkySettings },
         { "SunAngularDiameter", kSkySettings },
         { "SunGlow", kSkySettings },
         { "SunsetColor", kSkySettings },
         { "SunsetIntensity", kSkySettings },
         { "StarIntensity", kSkySettings },

         // The time-of-day driver turns these five into the sun's transform.
         { "DriveSunFromTimeOfDay", kTimeOfDay },
         { "TimeOfDay", kTimeOfDay },
         { "DayLengthSeconds", kTimeOfDay },
         { "Latitude", kTimeOfDay },
         { "NorthOffset", kTimeOfDay },

         // Environment-bake policy and size, carried in the same settings block.
         { "AutoRebakeEnvironment", kSkySettings },
         { "RebakeSunAngleThreshold", kSkySettings },
         { "EnvironmentResolution", kSkySettings },

         // Display-only state, and the widget is what maintains it.
         { "ActivePreset", kSkyWidget },

         // Shared by the sky and the cloud shell; converted to world units on the C++ side.
         { "PlanetRadius", kSkySettings },

         // ---- The physical atmosphere (Phase 0/1 of the Sky Atmosphere programme) ------------------
         // The medium group funnels through MakeSkySettings into the sky payload's medium block, where
         // the SkyTransmittanceLut / SkyMultiScatterLut compute passes read it — a fingerprint change
         // re-dispatches both, so each of these fields moves real GPU texels today.
         { "Model", kSkySettings }, // gates the LUT dispatch (SkyboxRenderer::ExecuteAtmosphereLuts)
         { "AtmosphereHeight", kSkySettings },
         { "MultiScatteringFactor", kSkySettings },
         { "GroundAlbedo", kSkySettings },
         { "RayleighScatteringScale", kSkySettings },
         { "RayleighScattering", kSkySettings },
         { "RayleighExponentialDistribution", kSkySettings },
         { "MieScatteringScale", kSkySettings },
         { "MieScattering", kSkySettings },
         { "MieAbsorptionScale", kSkySettings },
         { "MieAbsorption", kSkySettings },
         { "MieExponentialDistribution", kSkySettings },
         { "OtherAbsorptionScale", kSkySettings },
         { "OtherAbsorption", kSkySettings },
         { "AbsorptionTipAltitude", kSkySettings },
         { "AbsorptionTipValue", kSkySettings },
         { "AbsorptionTentWidth", kSkySettings },

         // Authored and carried in the payload for the passes the next phases add. PENDING, exactly like
         // the cloud fields were while their passes were being written: each names the phase that owes
         // it a reader (the phasing is Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 4).
         { "MieAnisotropy", nullptr,
           "Sky Phase 2 - Sky-View LUT: the Cornette-Shanks Mie phase of the scattering integrator" },
         { "SkyLuminanceFactor", nullptr,
           "Sky Phase 2 - physical sky pass: art-direction tint on sky pixels only" },
         { "SkyAndAerialPerspectiveLuminanceFactor", nullptr,
           "Sky Phase 2 - Sky-View LUT: art-direction tint inside every scattering integration" },
         { "AerialPerspectiveViewDistanceScale", nullptr,
           "Sky Phase 3 - camera aerial-perspective volume: scales the froxel march distance" },
         { "AerialPerspectiveStartDepth", nullptr,
           "Sky Phase 3 - aerial perspective on opaque: distance where the haze starts" },
    };

    // ------------------------------------------------------------------------------------------------
    // Clouds: the component is authored, the passes that read it are not written yet.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kCloudWidget =
         "Editor/Source/Editor/Panels/SceneProperties/ComponentWidgets/VolumetricCloudsComponent.cpp";

    // The four tasks that owe the cloud component its readers. Naming the task per field is what keeps a
    // forgotten field from disappearing into "the clouds are not done yet".
    // T7 has LANDED and consumes ShapeSeed, DetailSeed, Enabled and the regenerate request. It does not
    // consume the erosion-shaping fields below: those shape how the volumes are SAMPLED, which is the
    // raymarch's job. A debt booked against a finished task reads as paid, so they are owed by T8.
    // Both cloud-shadow fields are read on the CPU where the payload is packed: the extent through
    // CloudShadowExtentOf, which the shadow pass and the march both project with, and the toggle straight
    // into the block the march branches on.
    constexpr const char* kCloudPayload = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudPayload.hpp";

    constexpr const char* kT8 = "T8 - weather map and raymarch: samples the density field and lights it";
    constexpr const char* kT8Steps =
         "T8 - weather map and raymarch: the step schedule and the sampling budget of the march";
    constexpr const char* kT9 =
         "T9 - temporal resolve: how much of the previous frame survives, and how far it may drift";
    constexpr const char* kT8T10 =
         "T8/T10 - sizes the raymarch target that T8 writes and T10 upsamples into the frame";

    constexpr Row kCloudRows[] = {
         // Cloud Layer - the shell the ray is marched through.
         { "Enabled", nullptr, kT8 },
         { "LayerBottomAltitude", nullptr, kT8 },
         { "LayerThickness", nullptr, kT8 },
         { "MaxViewDistance", nullptr, kT8 },
         { "HorizonFadeStart", nullptr, kT8 },
         { "HorizonFadeEnd", nullptr, kT8 },

         // Weather - the coverage field.
         { "Coverage", nullptr, kT8 },
         { "CoverageContrast", nullptr, kT8 },
         { "WeatherTileSize", nullptr, kT8 },
         { "WeatherSeed", kCloudWidget }, // a seed change raises RequestRegenerateNoise; T7 rebuilds
         { "WeatherOctaves", nullptr, kT8 },
         { "WeatherWarpStrength", nullptr, kT8 },
         { "CloudType", nullptr, kT8 },
         { "CloudTypeVariance", nullptr, kT8 },
         { "AnvilBias", nullptr, kT8 },
         { "Wetness", nullptr, kT8 },

         // Shape - the base density field.
         { "ShapeTileSize", nullptr, kT8 },
         { "ShapeSeed", kCloudWidget },
         { "BaseShapeRemapMin", nullptr, kT8 },
         { "ShapeErosionStrength", nullptr, kT8 },
         { "ExtinctionScale", nullptr, kT8 },
         { "StratusGradient", nullptr, kT8 },
         { "StratocumulusGradient", nullptr, kT8 },
         { "CumulusGradient", nullptr, kT8 },
         { "BaseGradientPower", nullptr, kT8 },
         { "TopGradientPower", nullptr, kT8 },
         { "DensityHeightBias", nullptr, kT8 },

         // Detail - the erosion of that field.
         { "DetailStrength", nullptr, kT8 },
         { "DetailTileSize", nullptr, kT8 },
         { "DetailSeed", kCloudWidget },
         { "DetailTypeBias", nullptr, kT8 },
         { "BillowGradientPower", nullptr, kT8 },
         { "BillowNoiseScale", nullptr, kT8 },
         { "HighFreqStrength", nullptr, kT8 },
         { "HighFreqWispSharpness", nullptr, kT8 },
         { "HighFreqBillowSharpness", nullptr, kT8 },
         { "HighFreqFadeStart", nullptr, kT8 },
         { "HighFreqFadeEnd", nullptr, kT8 },
         { "CurlStrength", nullptr, kT8 },
         { "CurlTileSize", nullptr, kT8 },
         { "DensitySharpenLow", nullptr, kT8 },
         { "DensitySharpenHigh", nullptr, kT8 },
         { "DensityScalePower", nullptr, kT8 },
         { "DistanceSoftening", nullptr, kT8 },
         { "SofteningStartDistance", nullptr, kT8 },
         { "SofteningEndDistance", nullptr, kT8 },
         { "NearFadeStart", nullptr, kT8 },
         { "NearFadeEnd", nullptr, kT8 },
         { "NearFadeMinDensity", nullptr, kT8 },

         // Lighting - in-scatter, phase, ambient.
         { "ScatteringAlbedo", nullptr, kT8 },
         { "ExtinctionTint", nullptr, kT8 },
         { "LightMarchDistance", nullptr, kT8 },
         { "LightConeSpread", nullptr, kT8 },
         { "PhaseForwardG", nullptr, kT8 },
         { "PhaseBackwardG", nullptr, kT8 },
         { "PhaseBlend", nullptr, kT8 },
         { "SilverLiningIntensity", nullptr, kT8 },
         { "PowderStrength", nullptr, kT8 },
         { "PowderScale", nullptr, kT8 },
         { "MultiScatterExtinctionFalloff", nullptr, kT8 },
         { "MultiScatterScatterFalloff", nullptr, kT8 },
         { "MultiScatterPhaseFalloff", nullptr, kT8 },
         { "AmbientSkyContribution", nullptr, kT8 },
         { "AmbientGroundContribution", nullptr, kT8 },
         { "AmbientHeightBias", nullptr, kT8 },
         { "SunLightIntensityScale", nullptr, kT8 },
         { "SunTint", nullptr, kT8 },
         { "ShadowTint", nullptr, kT8 },
         { "PrecipitationDarkening", nullptr, kT8 },
         { "AtmosphericPerspective", nullptr, kT8 },
         { "DistanceFadeStart", nullptr, kT8 },
         { "DistanceFadeEnd", nullptr, kT8 },

         // Animation - the scroll offsets the march applies to its lookups.
         { "AnimationSpeed", nullptr, kT8 },
         { "WindInfluence", nullptr, kT8 },
         { "WindDirectionOffset", nullptr, kT8 },
         { "ShapeScrollMultiplier", nullptr, kT8 },
         { "DetailScrollMultiplier", nullptr, kT8 },
         { "WeatherScrollMultiplier", nullptr, kT8 },
         { "WindHeightShear", nullptr, kT8 },
         { "WindUpliftSpeed", nullptr, kT8 },

         // Quality - the cost dial.
         { "QualityLevel", kCloudWidget },
         { "ResolutionScale", nullptr, kT8T10 },
         { "MaxSteps", nullptr, kT8Steps },
         { "MinStepSize", nullptr, kT8Steps },
         { "MaxStepSize", nullptr, kT8Steps },
         { "StepGrowthRate", nullptr, kT8Steps },
         { "CoarseStepMultiplier", nullptr, kT8Steps },
         { "EmptySamplesBeforeCoarse", nullptr, kT8Steps },
         { "LightMarchSamples", nullptr, kT8Steps },
         { "MultiScatterOctaves", nullptr, kT8Steps },
         { "AmbientOcclusion", kCloudPayload },
         { "AutoDistanceFade", kCloudPayload },
         { "CloudShadowMap", kCloudPayload },
         { "CloudShadowExtent", kCloudPayload },
         { "TemporalMode", nullptr, kT9 },
         { "TemporalBlendFactor", nullptr, kT9 },
         { "TemporalClampScale", nullptr, kT9 },
         { "JitterStrength", nullptr, kT8Steps },

         // Preset - the selector the widget turns into the 78 fields above.
         { "Preset", kCloudWidget },
    };

    // ------------------------------------------------------------------------------------------------
    // Height fog: the component and its pass shipped together (Sky plan Phase 5), so every field is
    // WIRED - nothing pending. One funnel consumes them: PackFogParams in FogPayload.hpp turns each
    // field into the GPU block the fog pass evaluates; Enabled is the renderer's own dispatch gate.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kFogPayload = "Desert/Desert/Source/Engine/Graphic/Fog/FogPayload.hpp";
    constexpr const char* kFogRenderer =
         "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Fog/HeightFogRenderer.cpp";

    constexpr Row kFogRows[] = {
         { "Enabled", kFogRenderer }, // the zero-cost gate: off means no allocation and no dispatch

         { "FogDensity", kFogPayload },
         { "FogHeightFalloff", kFogPayload },
         { "FogInscatteringLuminance", kFogPayload },
         { "SkyAtmosphereAmbientContributionColorScale", kFogPayload },
         { "FogMaxOpacity", kFogPayload },
         { "StartDistance", kFogPayload },
         { "FogCutoffDistance", kFogPayload },

         { "SecondFogDensity", kFogPayload },
         { "SecondFogHeightFalloff", kFogPayload },
         { "SecondFogHeightOffset", kFogPayload },

         { "DirectionalInscatteringExponent", kFogPayload },
         { "DirectionalInscatteringStartDistance", kFogPayload },
         { "DirectionalInscatteringLuminance", kFogPayload },
    };

    // The repository root, found by walking up from wherever the test binary was started - the same
    // approach the font-baker test uses, so neither has to be run from one exact directory.
    std::string RepoRoot()
    {
        // Starts at "./" rather than "": an empty string is this function's "not found", and the root is
        // very often the directory the test was started in.
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/ECS/Components.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadFile( const std::string& path )
    {
        std::ifstream in( path );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // Whole-word search, so "Preset" does not match inside "CloudPresetValues" and report a consumer that
    // merely names the type.
    bool MentionsWord( const std::string& haystack, const std::string& word )
    {
        const auto boundary = []( char c )
        { return !( std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' ); };
        for ( std::size_t at = haystack.find( word ); at != std::string::npos; at = haystack.find( word, at + 1 ) )
        {
            const bool leftOk  = at == 0 || boundary( haystack[at - 1] );
            const bool rightOk = at + word.size() >= haystack.size() || boundary( haystack[at + word.size()] );
            if ( leftOk && rightOk )
                return true;
        }
        return false;
    }

    const TypeInfo& Type( const char* name )
    {
        const TypeInfo* t = ReflectionRegistry::Get().Find( name );
        EXPECT_NE( t, nullptr ) << name << " is not registered - the codegen did not run";
        return *t;
    }

    // Every reflected field appears in the table exactly once, and every table row names a real field.
    void CheckTableCoversTypeExactly( const TypeInfo& type, const Row* rows, std::size_t count )
    {
        for ( const FieldInfo& f : type.Fields )
        {
            const std::ptrdiff_t hits =
                 std::count_if( rows, rows + count, [&f]( const Row& r ) { return f.Name == r.Field; } );
            EXPECT_EQ( hits, 1 ) << type.Name << "::" << f.Name
                                 << " has no consumer row (or more than one). Every exposed setting must "
                                    "name either the code that reads it or the task that will.";
        }

        for ( const Row* r = rows; r != rows + count; ++r )
        {
            const bool known = std::any_of( type.Fields.begin(), type.Fields.end(),
                                            [r]( const FieldInfo& f ) { return f.Name == r->Field; } );
            EXPECT_TRUE( known ) << r->Field << " is listed as consumed but is not a field of " << type.Name
                                 << " - a stale row outliving the field it described";

            EXPECT_TRUE( ( r->Where == nullptr ) != ( r->Task == nullptr ) )
                 << r->Field << " must name either a consumer file or an owing task, not both or neither";
        }

        EXPECT_EQ( count, type.Fields.size() );
    }

    // For the wired rows: the named file exists and really does mention the field.
    void CheckWiredRowsReadTheirField( const std::string& root, const Row* rows, std::size_t count )
    {
        for ( const Row* r = rows; r != rows + count; ++r )
        {
            if ( !r->Where )
                continue;

            const std::string text = ReadFile( root + r->Where );
            ASSERT_FALSE( text.empty() ) << "named consumer " << r->Where << " could not be read";
            EXPECT_TRUE( MentionsWord( text, r->Field ) )
                 << r->Where << " is named as the consumer of " << r->Field
                 << " but does not mention it - the read was removed and the setting is now dead";
        }
    }
} // namespace

TEST( SettingConsumers, EverySkyFieldNamesItsConsumer )
{
    CheckTableCoversTypeExactly( Type( "SkyAtmosphereData" ), kSkyRows, std::size( kSkyRows ) );
}

TEST( SettingConsumers, EveryCloudFieldNamesItsConsumerOrTheTaskThatOwesOne )
{
    CheckTableCoversTypeExactly( Type( "VolumetricCloudData" ), kCloudRows, std::size( kCloudRows ) );
}

TEST( SettingConsumers, EveryFogFieldNamesItsConsumer )
{
    CheckTableCoversTypeExactly( Type( "ExponentialHeightFogData" ), kFogRows, std::size( kFogRows ) );
}

TEST( SettingConsumers, EveryNamedConsumerActuallyReadsTheFieldItClaims )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found - run from the workspace root or build/Bin";

    CheckWiredRowsReadTheirField( root, kSkyRows, std::size( kSkyRows ) );
    CheckWiredRowsReadTheirField( root, kCloudRows, std::size( kCloudRows ) );
    CheckWiredRowsReadTheirField( root, kFogRows, std::size( kFogRows ) );
}

// The fog shipped WHOLE - component, pass and couplings in one task (Sky plan Phase 5) - so it owes
// nothing. This pin is what keeps that true: a field added without its reader turns this zero into a
// reviewable edit.
TEST( SettingConsumers, TheFogComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kFogRows ), std::end( kFogRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// The artistic-gradient sky is finished and owes nothing; the PHYSICAL atmosphere is being built in
// phases (Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 4), and after Phase 0/1 exactly five of its
// fields await their reader: the Mie phase g and the four art-direction factors, all consumed by the
// Phase 2/3 integrations. A count, like the cloud one below, so the debt cannot quietly grow — when a
// sky phase lands, this number drops and the drop is a reviewable edit.
TEST( SettingConsumers, TheSkyComponentOwesExactlyThePhase2And3Fields )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kSkyRows ), std::end( kSkyRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 5 );
}

// A count, so that "the clouds are not wired yet" cannot quietly grow to cover a field nobody meant to
// leave out. When a cloud pass lands, this number drops and the drop is a reviewable edit.
TEST( SettingConsumers, TheCloudComponentOwesExactlyTheFieldsItsPassesHaveNotBeenWrittenFor )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kCloudRows ), std::end( kCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    // 95 fields, five of which the Details widget already consumes: the two selectors and the three
    // noise seeds.
    EXPECT_EQ( pending, 90 );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
