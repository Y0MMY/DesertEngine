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
    // Sky: every field is wired. The artistic-gradient group through the sky pass and the IBL bake, the
    // physical-atmosphere group through the LUT passes and the Phase 2 sky pass, and the
    // aerial-perspective group through the Phase 3 froxel volume and the atmospheric-fog pass.
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

         // Wired by Phase 2: MieAnisotropy is the Cornette-Shanks g of the scattering integrator
         // (Common/SkyScattering.glslh via the SkyViewLut / BakeProceduralSky marches); the two
         // art-direction tints funnel through MakeSkySettings into the payload's Phase 2 lanes, read
         // by the physical sky pass (SkyLuminanceFactor, on-screen pixels only) and inside every
         // scattering integration (SkyAndAerialPerspectiveLuminanceFactor).
         { "MieAnisotropy", kSkySettings },
         { "SkyLuminanceFactor", kSkySettings },
         { "SkyAndAerialPerspectiveLuminanceFactor", kSkySettings },

         // Wired by Phase 3: all three funnel through MakeSkySettings into SkySettings, from where
         // SkyboxRenderer fills the 32x32x16 aerial-perspective volume (start depth and distance, on
         // the fill's push block) and the atmospheric-fog pass reads it (distance and view-distance
         // scale, published on AtmosphereEnv). Every one of them moves real froxels today.
         { "AerialPerspectiveViewDistanceScale", kSkySettings },
         { "AerialPerspectiveStartDepth", kSkySettings },
         { "AerialPerspectiveDistance", kSkySettings },
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

    // The Cloud Type axis is baked into a texture on the CPU, so the six profile gradients and the three
    // form bends have a named C++ reader rather than a shader that samples them: this file turns them
    // into the table the march looks the profile up in.
    constexpr const char* kCloudCurves = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudProfileCurves.hpp";

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
         { "CloudHeightVariance", kCloudPayload },
         { "AnvilBias", nullptr, kT8 },
         { "Wetness", nullptr, kT8 },

         // Shape - the base density field.
         { "ShapeTileSize", nullptr, kT8 },
         { "ShapeSeed", kCloudWidget },
         { "BaseShapeRemapMin", nullptr, kT8 },
         { "ShapeErosionStrength", nullptr, kT8 },
         { "ExtinctionScale", nullptr, kT8 },
         { "StratusGradient", kCloudCurves },
         { "StratocumulusGradient", kCloudCurves },
         { "CumulusGradient", kCloudCurves },
         { "ShelfGradient", kCloudCurves },
         { "ShelfProfileForm", kCloudCurves },
         { "CongestusGradient", kCloudCurves },
         { "CongestusProfileForm", kCloudCurves },
         { "AnvilGradient", kCloudCurves },
         { "AnvilProfileForm", kCloudCurves },
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

    // ------------------------------------------------------------------------------------------------
    // Cloud Volume: a placed hero cloud (Docs/Clouds/VOXEL_CLOUD_PATH.md phase 1). Phase 1a delivered the
    // .dvol format, the analytic baker, the asset and this component; the seam that reads them - the
    // voxel density header, the instance buffer and the atlas binding - is phase 1b, which is a separate
    // task because a different developer owns the cloud shaders. So every field here is PENDING against
    // that one task, and the count below is what stops "the seam is not wired yet" from quietly growing
    // to cover a field nobody meant to leave out.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kVoxel1bGate =
         "Voxel phase 1b - the density seam: whether this instance is gathered into the buffer at all";
    constexpr const char* kVoxel1bAtlas =
         "Voxel phase 1b - the density seam: resolves the .dvol into an atlas tile and puts the tile "
         "origin and the world-to-local transform in the instance record";
    constexpr const char* kVoxel1bScale =
         "Voxel phase 1b - the density seam: a per-instance multiplier the shader applies to the baked "
         "Density Scale channel";
    constexpr const char* kVoxel1bType =
         "Voxel phase 1b - the density seam: a per-instance bias the shader applies to the baked Detail "
         "Type channel before the erosion reads it";
    constexpr const char* kVoxel1bShadow =
         "Voxel phase 1b - the density seam: whether CloudShadowMap.shader marches this instance as well "
         "as the view";

    constexpr Row kCloudVolumeRows[] = {
         { "Enabled", nullptr, kVoxel1bGate },
         { "Volume", nullptr, kVoxel1bAtlas },
         { "DensityScale", nullptr, kVoxel1bScale },
         { "DetailTypeBias", nullptr, kVoxel1bType },
         { "CastsCloudShadow", nullptr, kVoxel1bShadow },
    };

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

TEST( SettingConsumers, EveryCloudVolumeFieldNamesItsConsumerOrTheTaskThatOwesOne )
{
    CheckTableCoversTypeExactly( Type( "CloudVolumeData" ), kCloudVolumeRows, std::size( kCloudVolumeRows ) );
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
    CheckWiredRowsReadTheirField( root, kCloudVolumeRows, std::size( kCloudVolumeRows ) );
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

// The sky component now owes NOTHING. The artistic gradient was always finished; the physical
// atmosphere was built in phases (Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 4) and Phase 3 — the
// camera aerial-perspective volume and its apply on opaque — consumed the last two fields that were
// carried without a reader, plus the Aerial Perspective Distance it added.
//
// The count stays as a count rather than becoming "no PENDING rows exist", because the remaining
// phases (4: the distant sky-light value; the cloud march sampling this volume) may well add a field
// before they add its reader. When that happens the number rises in a reviewable edit instead of a
// field quietly joining the component with nobody accountable for it.
TEST( SettingConsumers, TheSkyComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kSkyRows ), std::end( kSkyRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// A count, so that "the clouds are not wired yet" cannot quietly grow to cover a field nobody meant to
// leave out. When a cloud pass lands, this number drops and the drop is a reviewable edit.
TEST( SettingConsumers, TheCloudComponentOwesExactlyTheFieldsItsPassesHaveNotBeenWrittenFor )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kCloudRows ), std::end( kCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    // 87, down from 90: the three legacy profile gradients now have a named C++ reader instead of a
    // task that owes them one. The Cloud Type axis is baked into a texture on the CPU, so
    // CloudProfileCurves.hpp reads them — together with the six fields the authored forms added, and
    // Cloud Height Variance, which the payload clamps and packs.
    EXPECT_EQ( pending, 87 );
}

// The hero-cloud component landed one phase ahead of the seam that reads it, deliberately: the .dvol
// format, the baker, the asset and this component are one task, and the shader-side seam is another
// because a different developer owns the cloud shaders. All five fields are therefore owed by phase 1b,
// and this five is what turns "the seam landed" into a reviewable edit that drives it to zero.
TEST( SettingConsumers, TheCloudVolumeComponentOwesEveryFieldToTheSeamThatHasNotLandedYet )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kCloudVolumeRows ), std::end( kCloudVolumeRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 5 );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
