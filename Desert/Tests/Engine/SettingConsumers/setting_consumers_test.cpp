// Does every exposed setting actually reach something?
//
// A slider that moves nothing is the failure mode this programme was written to avoid, and it is not
// caught by any build: an unread field compiles, serializes, appears in Details and does nothing at all.
// So the reflected field list of the sky and fog components is enumerated here and matched against an
// explicit table that says, for every single field, WHO consumes it.
//
// The table has two kinds of row:
//
//   * WIRED   - names a source file that must exist and must mention the field. If someone deletes the
//               read while leaving the field, that file stops mentioning it and this test goes red.
//   * PENDING - names the TASK that owes the field a consumer. Nothing is pending today, and the two
//               counts at the bottom of this file pin that. The kind exists so that a component whose
//               passes are still being written can say so PER FIELD, which is the difference between
//               "not built yet" and "forgotten": a field nobody ever claims is a field with no owner.
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

         // Converted to world units on the C++ side.
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

    // ------------------------------------------------------------------------------------------------
    // Volumetric clouds: every field is WIRED, and there are three consumers rather than one because the
    // component's fields reach the GPU by three different routes.
    //
    //   * PackCloudParams turns the per-frame settings into the twelve-vec4 block the march reads. It
    //     lives in CloudPayload.hpp.
    //   * The ECS system owns the timestep, so the two wind fields are integrated there into the offset
    //     the packer is handed - the component carries no accumulated state of its own.
    //   * Enabled is the renderer's dispatch gate: off allocates nothing and dispatches nothing, which is
    //     a decision the packer cannot make because it runs after it. NoiseVolume is the renderer's too:
    //     it is a handle, not a number, and EnsureNoiseVolume resolves it through
    //     Runtime::CloudNoiseService into the Image3D the march samples.
    //
    // WHAT USED TO BE HERE AND IS NOT. Four rows - WeatherSeed, WeatherOctaves, DetailSeed and
    // DetailOctaves - pointed at a bake key that turned them into the push constant of a compute pass.
    // That pass is gone: the noise volume is an asset generated offline, its seed and lattice periods live
    // in the container's own header, and the component names the volume instead of describing how to bake
    // one. Four rows removed rather than repointed, because there is nothing left for them to point at.
    //
    // And four more since: LayerBottomAltitude and LayerThickness stated the shell by hand, which the
    // cloud type now states in kilometres and the packer computes; the old scalar CloudType and its
    // variance drove one analytic profile curve, which is now a per-type TABLE indexed by the placement
    // pattern's own value. Every one of the four was removed rather than repointed.
    //
    // And ONE more with T1: NoiseVolume. It was not removed - it MOVED, onto the cloud type asset, because
    // the character of a cloud's edge is a property of the kind of cloud rather than of the layer's
    // weather. There is no row for it here because it is no longer a field of this component; the row that
    // replaced it is CloudType, and the renderer is what resolves that handle into both the twelve numbers
    // and the volume.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kCloudPayload = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudPayload.hpp";
    constexpr const char* kCloudSystem  = "Desert/Desert/Source/Engine/ECS/System/VolumetricCloudECSSystem.hpp";
    constexpr const char* kCloudRenderer =
         "Desert/Desert/Source/Engine/Graphic/Systems/Scene/Clouds/VolumetricCloudRenderer.cpp";

    constexpr Row kCloudRows[] = {
         { "Enabled", kCloudRenderer }, // the zero-cost gate: off means no allocation and no dispatch

         // Cloud Layer - the shell the march intersects. The CLOUD TYPES are what the shell is built
         // from, and unlike every other row here they are resolved rather than packed: the renderer turns
         // each handle into fourteen numbers through Runtime::CloudTypeService, and those numbers become
         // Layer.y/Layer.z (the shell, the UNION of the set's bands) and one entry each of SpeciesEdge
         // and SpeciesPlacement. Two fields that used to state the shell by hand are gone, because an
         // authored shell and a type's altitudes are two numbers obliged to agree.
         //
         // FOUR ROWS AND NOT ONE SINCE T3: a layer carries a SET of kinds of cloud. Every one of them has
         // to have a consumer or it is a slot an artist can fill and never see - which is the exact shape
         // of the defect this whole suite exists to catch, and the easiest one to introduce by wiring only
         // the first slot.
         { "CloudType1", kCloudRenderer },
         { "CloudType2", kCloudRenderer },
         { "CloudType3", kCloudRenderer },
         { "CloudType4", kCloudRenderer },
         { "PlanetRadius", kCloudPayload },
         { "MaxViewDistance", kCloudPayload },
         { "TracingStartDistance", kCloudPayload },
         { "TracingStartMaxDistance", kCloudPayload },

         // Weather - the coverage field.
         { "Coverage", kCloudPayload },
         { "CoverageContrast", kCloudPayload },
         { "WeatherTileSize", kCloudPayload },

         // Detail - the erosion field.
         { "DetailTileSize", kCloudPayload },
         { "DetailStrength", kCloudPayload },
         { "DensityScale", kCloudPayload },
         { "NearFadeStartDistance", kCloudPayload },
         { "NearFadeEndDistance", kCloudPayload },
         { "ExtinctionScale", kCloudPayload },

         // Lighting.
         { "ScatteringAlbedo", kCloudPayload },
         { "PhaseG", kCloudPayload },
         { "PhaseGBackward", kCloudPayload },
         { "PhaseBlend", kCloudPayload },
         { "AmbientOcclusionStrength", kCloudPayload },
         { "AerialPerspectiveStartDistance", kCloudPayload },
         { "AerialPerspectiveFadeDistance", kCloudPayload },
         { "LightMarchDistance", kCloudPayload },
         { "LightMarchSamples", kCloudPayload },
         { "MultiScatterOctaves", kCloudPayload },
         { "MultiScatterContribution", kCloudPayload },
         { "MultiScatterOcclusion", kCloudPayload },
         { "MultiScatterEccentricity", kCloudPayload },
         { "AmbientScale", kCloudPayload },

         // Shadows on the world. NEITHER GOES THROUGH THE PACKER, and that is the one thing worth
         // knowing about this pair: the shadow map is not part of CloudGpuPayload at all. `CastShadows`
         // is the zero-cost gate the renderer tests before it allocates or dispatches anything, and
         // `ShadowStrength` reaches the GPU through the CONSUMER — CloudShadowUniforms::Params.w in
         // MaterialDeferredLighting — because the map holds the medium's own physical numbers and the
         // artist's dial is applied where the transmittance is reconstructed. Both are read by
         // VolumetricCloudRenderer::GetShadowStrength(), which is the one place the two are combined.
         { "CastShadows", kCloudRenderer },
         { "ShadowStrength", kCloudRenderer },

         // Quality.
         { "MaxSteps", kCloudPayload },
         { "StopTransmittance", kCloudPayload },

         // Animation - integrated against the timestep by the system that owns it, and handed to the
         // packer as an offset.
         { "WindDirection", kCloudSystem },
         { "WindSpeed", kCloudSystem },
    };

    // ------------------------------------------------------------------------------------------------
    // The HERO CLOUD - slot A of the seam, one sculpted body placed by an entity's own transform.
    //
    // ITS FIELDS SPLIT THREE WAYS AND EACH WAY MEANS SOMETHING. `Enabled` and `Volume` are the ECS
    // system's and the renderer's: the first decides whether the instance is COLLECTED at all (which is
    // what makes a disabled hero cloud cost nothing rather than nearly nothing), the second is a handle
    // the renderer resolves through Runtime::CloudModellingService. Everything else is packed, and the
    // packer is its own file rather than CloudPayload.hpp because a hero cloud is a per-frame LIST and
    // the layer is one block.
    //
    // There is no row for a transform here, and that is the point of the component's shape: WHERE the
    // cloud is comes from the entity, so there is no authored position to leave unread.
    // ------------------------------------------------------------------------------------------------

    constexpr const char* kHeroPayload = "Desert/Desert/Source/Engine/Graphic/Clouds/CloudAuthoredPayload.hpp";

    constexpr Row kHeroCloudRows[] = {
         { "Enabled", kCloudSystem },  // the zero-cost gate: not collected, so the march's loop is empty
         { "Volume", kCloudRenderer }, // resolved through the modelling service, never packed as a number
         { "Strength", kHeroPayload },         { "SuppressProceduralField", kHeroPayload },
         { "DetailFactor", kHeroPayload },     { "DensityFactor", kHeroPayload },
         { "ExtinctionFactor", kHeroPayload },
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

    // Whole-word search, so "Preset" does not match inside "SkyPresetValues" and report a consumer that
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

TEST( SettingConsumers, EveryFogFieldNamesItsConsumer )
{
    CheckTableCoversTypeExactly( Type( "ExponentialHeightFogData" ), kFogRows, std::size( kFogRows ) );
}

TEST( SettingConsumers, EveryCloudFieldNamesItsConsumer )
{
    CheckTableCoversTypeExactly( Type( "VolumetricCloudData" ), kCloudRows, std::size( kCloudRows ) );
}

TEST( SettingConsumers, EveryHeroCloudFieldNamesItsConsumer )
{
    CheckTableCoversTypeExactly( Type( "HeroCloudData" ), kHeroCloudRows, std::size( kHeroCloudRows ) );
}

TEST( SettingConsumers, EveryNamedConsumerActuallyReadsTheFieldItClaims )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found - run from the workspace root or build/Bin";

    CheckWiredRowsReadTheirField( root, kSkyRows, std::size( kSkyRows ) );
    CheckWiredRowsReadTheirField( root, kFogRows, std::size( kFogRows ) );
    CheckWiredRowsReadTheirField( root, kCloudRows, std::size( kCloudRows ) );
    CheckWiredRowsReadTheirField( root, kHeroCloudRows, std::size( kHeroCloudRows ) );
}

// Slot A shipped WHOLE - format, loader, service, component, collector, packer, seam and cutout in one
// task - so it owes nothing either. The pin is what keeps that true.
TEST( SettingConsumers, TheHeroCloudComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kHeroCloudRows ), std::end( kHeroCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
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
// The count stays as a count rather than becoming "no PENDING rows exist", because a later phase may
// well add a field before it adds its reader. When that happens the number rises in a reviewable edit
// instead of a field quietly joining the component with nobody accountable for it.
TEST( SettingConsumers, TheSkyComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kSkyRows ), std::end( kSkyRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// The cloud component shipped the same way the fog did - component, packer, bake and march in one
// programme - so it owes nothing either. The count is a count rather than "no PENDING rows exist" for the
// reason the two above give: a later phase may well add a field before it adds its reader, and when that
// happens the number has to rise in a reviewable edit instead of a field quietly joining the component
// with nobody accountable for it.
//
// It matters more here than anywhere else in this file. A cloud parameter that does nothing still LOOKS
// like it does, because the sky it is supposed to change is already busy - which is precisely why this
// programme's contract forbids a knob that moves nothing.
TEST( SettingConsumers, TheCloudComponentOwesNothing )
{
    const std::ptrdiff_t pending = std::count_if( std::begin( kCloudRows ), std::end( kCloudRows ),
                                                  []( const Row& r ) { return r.Task != nullptr; } );

    EXPECT_EQ( pending, 0 );
}

// A slider can name its consumer and STILL not reach it, and this is the case that proves it.
//
// Light March Samples travels to the GPU through three separate ceilings: the Range on the PROPERTY, the
// std::clamp in Graphic::PackCloudParams, and a clamp written again inside the compute shader. All three
// were the literal 16. Raise the first two to 64 and forget the third and the artist drags the slider to
// 64, the payload carries 64, and the march silently uses 16 — a setting that reaches its consumer and is
// thrown away there, which no reflection test and no build can see.
//
// The first two ceilings are now one constant. The shader cannot include a C++ header, so its copy is
// checked the only way it can be: by reading the shader's own text. That is what makes this an assertion
// about the RELATION (contract 2.3.1) rather than three assertions about the number 64.
TEST( SettingConsumers, TheShaderClampsTheShadowRayAtTheSameCeilingTheSliderOffers )
{
    const std::string root = RepoRoot();
    ASSERT_FALSE( root.empty() ) << "repository root not found from the test's working directory";

    const std::string path   = root + "Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader";
    const std::string source = ReadFile( path );
    ASSERT_FALSE( source.empty() ) << path << " is missing or empty";

    // The exact line the march clamps on. Written out in full rather than matched loosely, because a
    // regex that still matched after somebody rewrote the clamp would pass while testing nothing.
    const std::string expected = "int   lightSamples = int(clamp(u_CloudSunColour.w, 1.0f, " +
                                 std::to_string( Desert::ECS::kCloudLightMarchMaxSamples ) + ".0f));";

    EXPECT_NE( source.find( expected ), std::string::npos )
         << "CloudRaymarch.shader does not clamp the shadow ray's sample count at "
         << Desert::ECS::kCloudLightMarchMaxSamples << ", which is the ceiling the slider offers and the "
         << "payload packs. Expected to find:\n  " << expected;
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
