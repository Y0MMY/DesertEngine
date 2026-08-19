// What the sky, fog and light components actually EXPOSE, checked against the specification field by field.
//
// The Details panel, scene serialization, undo, duplicate, prefabs and the Lua bindings are all driven by
// one table: the reflection metadata DesertHeaderTool generates from REFLECT()/PROPERTY(). A field that is
// mistyped, mis-categorised, or simply never annotated does not fail to compile — it silently does not
// exist. That is exactly the failure this test exists to catch, and it is checkable without a GPU: the
// generated translation unit is compiled straight into this binary and its static initializers fill the
// registry before main().

#include <Common/Core/Units.hpp>

// The cloud packer, for the near-fade relation below: the component's fields are only half of that
// story, and the half that can be undefined behaviour is the one on the GPU side of PackCloudParams.
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Reflection/ReflectionRegistry.hpp>
#include <Engine/Reflection/ReflectionTypes.hpp>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using Desert::Reflection::FieldInfo;
using Desert::Reflection::FieldType;
using Desert::Reflection::ReflectionRegistry;
using Desert::Reflection::TypeInfo;

namespace
{
    const TypeInfo& Type( const char* name )
    {
        const TypeInfo* t = ReflectionRegistry::Get().Find( name );
        EXPECT_NE( t, nullptr ) << "reflected type '" << name << "' is not registered at all";
        return *t;
    }

    const FieldInfo* Find( const TypeInfo& type, const char* name )
    {
        const auto it = std::find_if( type.Fields.begin(), type.Fields.end(),
                                      [name]( const FieldInfo& f ) { return f.Name == name; } );
        return it == type.Fields.end() ? nullptr : &*it;
    }

    std::vector<std::string> FieldNames( const TypeInfo& type )
    {
        std::vector<std::string> names;
        names.reserve( type.Fields.size() );
        for ( const auto& f : type.Fields )
            names.push_back( f.Name );
        return names;
    }

    std::size_t CountInCategory( const TypeInfo& type, const char* category )
    {
        return static_cast<std::size_t>( std::count_if( type.Fields.begin(), type.Fields.end(),
                                                        [category]( const FieldInfo& f )
                                                        { return f.Meta.Category == category; } ) );
    }

    // Reads a field out of the type's default-constructed instance, i.e. the member initializer as written.
    template <class T>
    T DefaultOf( const TypeInfo& type, const char* name )
    {
        const FieldInfo* f = Find( type, name );
        EXPECT_NE( f, nullptr ) << "no field '" << name << "'";
        EXPECT_NE( type.GetDefaultInstance, nullptr ) << "codegen emitted no default instance";
        T           value{};
        const auto* base = static_cast<const unsigned char*>( type.GetDefaultInstance() );
        std::memcpy( &value, base + f->Offset, sizeof( T ) );
        return value;
    }

    bool EndsWith( const std::string& s, const char* suffix )
    {
        const std::string suf( suffix );
        return s.size() >= suf.size() && s.compare( s.size() - suf.size(), suf.size(), suf ) == 0;
    }
} // namespace

// ---------------------------------------------------------------------------------------------------
// SkyAtmosphereData — 47 fields: the 24 artistic-gradient fields in their original order, then the 23
// physical-atmosphere fields (UE parameter names and grouping, Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md
// section 1.7, plus Aerial Perspective Distance, which UE keeps as an engine cvar and this engine has
// to author per scene — see the field's own comment), appended so the migration counters and the
// Details order both stay stable
// ---------------------------------------------------------------------------------------------------

TEST( SkyAtmosphereReflection, ExposesExactlyTheSpecifiedFieldsInOrder )
{
    const std::vector<std::string> expected = {
         "Enabled",
         "SkyBrightness",
         "HorizonFalloff",
         "ZenithColor",
         "HorizonColor",
         "GroundColor",
         "NightColor",
         "SunIntensity",
         "SunColor",
         "SunAngularDiameter",
         "SunGlow",
         "SunsetColor",
         "SunsetIntensity",
         "StarIntensity",
         "DriveSunFromTimeOfDay",
         "TimeOfDay",
         "DayLengthSeconds",
         "Latitude",
         "NorthOffset",
         "AutoRebakeEnvironment",
         "RebakeSunAngleThreshold",
         "EnvironmentResolution",
         "ActivePreset",
         "PlanetRadius",
         "Model",
         "AtmosphereHeight",
         "MultiScatteringFactor",
         "GroundAlbedo",
         "RayleighScatteringScale",
         "RayleighScattering",
         "RayleighExponentialDistribution",
         "MieScatteringScale",
         "MieScattering",
         "MieAbsorptionScale",
         "MieAbsorption",
         "MieAnisotropy",
         "MieExponentialDistribution",
         "OtherAbsorptionScale",
         "OtherAbsorption",
         "AbsorptionTipAltitude",
         "AbsorptionTipValue",
         "AbsorptionTentWidth",
         "SkyLuminanceFactor",
         "SkyAndAerialPerspectiveLuminanceFactor",
         "AerialPerspectiveViewDistanceScale",
         "AerialPerspectiveStartDepth",
         "AerialPerspectiveDistance",
    };

    const TypeInfo& sky = Type( "SkyAtmosphereData" );
    EXPECT_EQ( sky.Fields.size(), 47u );
    EXPECT_EQ( FieldNames( sky ), expected );
}

// The names 2-9 and 11-14 are byte-identical to the SkyboxComponent fields they replace, so the scene
// migration is a straight copy with no per-field conversion. A rename here would silently reset them to
// their defaults on load, because the reflection serializer has no field-alias mechanism.
TEST( SkyAtmosphereReflection, PaletteFieldNamesAreUnchangedFromTheComponentTheyReplace )
{
    const TypeInfo& sky = Type( "SkyAtmosphereData" );
    for ( const char* name :
          { "SkyBrightness", "HorizonFalloff", "ZenithColor", "HorizonColor", "GroundColor", "NightColor",
            "SunIntensity", "SunColor", "SunGlow", "SunsetColor", "SunsetIntensity", "StarIntensity" } )
        EXPECT_NE( Find( sky, name ), nullptr ) << name;
}

TEST( SkyAtmosphereReflection, CategoriesAndTypesMatchTheSpecification )
{
    const TypeInfo& sky = Type( "SkyAtmosphereData" );

    // enabled, brightness, falloff, preset, radius + the Sky Model switch
    EXPECT_EQ( CountInCategory( sky, "Atmosphere" ), 6u );
    EXPECT_EQ( CountInCategory( sky, "Sky Color" ), 4u ); // the four palette colours
    EXPECT_EQ( CountInCategory( sky, "Sun" ), 6u );
    EXPECT_EQ( CountInCategory( sky, "Night Sky" ), 1u );
    EXPECT_EQ( CountInCategory( sky, "Time Of Day" ), 5u );
    EXPECT_EQ( CountInCategory( sky, "Environment Lighting" ), 3u );

    // The physical-atmosphere groups, mirroring UE's Details panel grouping.
    EXPECT_EQ( CountInCategory( sky, "Physical Atmosphere" ), 3u ); // height, multi-scatter, albedo
    EXPECT_EQ( CountInCategory( sky, "Rayleigh" ), 3u );
    EXPECT_EQ( CountInCategory( sky, "Mie" ), 6u );
    EXPECT_EQ( CountInCategory( sky, "Absorption" ), 5u );
    EXPECT_EQ( CountInCategory( sky, "Art Direction" ), 5u );

    for ( const char* name :
          { "ZenithColor", "HorizonColor", "GroundColor", "NightColor", "SunColor", "SunsetColor", "GroundAlbedo",
            "RayleighScattering", "MieScattering", "MieAbsorption", "OtherAbsorption", "SkyLuminanceFactor",
            "SkyAndAerialPerspectiveLuminanceFactor" } )
    {
        const FieldInfo* f = Find( sky, name );
        ASSERT_NE( f, nullptr ) << name;
        EXPECT_EQ( f->Type, FieldType::Vec3 ) << name;
        EXPECT_TRUE( f->Meta.IsColor ) << name << " must draw as a colour picker";
    }

    // The three quality knobs fold away; nothing else does.
    EXPECT_EQ( std::count_if( sky.Fields.begin(), sky.Fields.end(),
                              []( const FieldInfo& f ) { return f.Meta.Advanced; } ),
               3 );
}

// UE's authored defaults for Earth (SkyAtmosphereComponent.cpp constructor, via the research doc):
// a UE-calibrated atmosphere must transplant number for number, so the defaults are pinned here rather
// than trusted to survive refactors. Scale x colour must multiply out to the physical coefficients.
TEST( SkyAtmosphereReflection, PhysicalDefaultsAreUEsEarth )
{
    const TypeInfo& sky = Type( "SkyAtmosphereData" );

    EXPECT_EQ( DefaultOf<uint8_t>( sky, "Model" ), 0u ) << "old scenes must load as ArtisticGradient";

    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "AtmosphereHeight" ), 60.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "MultiScatteringFactor" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "RayleighExponentialDistribution" ), 8.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "MieExponentialDistribution" ), 1.2f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "MieAnisotropy" ), 0.8f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "AbsorptionTipAltitude" ), 25.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "AbsorptionTentWidth" ), 15.0f );

    // Rayleigh beta = (0.005802, 0.013558, 0.033100) / km — stored as colour x scale, as UE stores it.
    const float     rayleighScale  = DefaultOf<float>( sky, "RayleighScatteringScale" );
    const glm::vec3 rayleighColour = DefaultOf<glm::vec3>( sky, "RayleighScattering" );
    EXPECT_NEAR( rayleighScale * rayleighColour.x, 0.005802f, 1e-5f );
    EXPECT_NEAR( rayleighScale * rayleighColour.y, 0.013558f, 1e-5f );
    EXPECT_NEAR( rayleighScale * rayleighColour.z, 0.033100f, 1e-5f );

    EXPECT_NEAR( DefaultOf<float>( sky, "MieScatteringScale" ), 0.003996f, 1e-6f );
    EXPECT_NEAR( DefaultOf<float>( sky, "MieAbsorptionScale" ), 0.000444f, 1e-6f );

    // Ozone absorption = (0.000650, 0.001881, 0.000085) / km.
    const float     ozoneScale  = DefaultOf<float>( sky, "OtherAbsorptionScale" );
    const glm::vec3 ozoneColour = DefaultOf<glm::vec3>( sky, "OtherAbsorption" );
    EXPECT_NEAR( ozoneScale * ozoneColour.x, 0.000650f, 1e-6f );
    EXPECT_NEAR( ozoneScale * ozoneColour.y, 0.001881f, 1e-6f );
    EXPECT_NEAR( ozoneScale * ozoneColour.z, 0.000085f, 1e-6f );

    // The art-direction factors default to "physical": white and one.
    EXPECT_EQ( DefaultOf<glm::vec3>( sky, "SkyLuminanceFactor" ), glm::vec3( 1.0f ) );
    EXPECT_EQ( DefaultOf<glm::vec3>( sky, "SkyAndAerialPerspectiveLuminanceFactor" ), glm::vec3( 1.0f ) );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "AerialPerspectiveViewDistanceScale" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( sky, "AerialPerspectiveStartDepth" ), 0.1f );
}

TEST( SkyAtmosphereReflection, SunAngularDiameterIsDegreesAndMatchesTheRadiusItReplaces )
{
    const TypeInfo&  sky = Type( "SkyAtmosphereData" );
    const FieldInfo* f   = Find( sky, "SunAngularDiameter" );
    ASSERT_NE( f, nullptr );
    EXPECT_EQ( f->Meta.Units, "deg" );
    EXPECT_FALSE( f->Meta.IsLength ) << "an angle is not a world distance";

    // The old field was a RADIUS in radians defaulting to 0.02. Degrees-of-diameter must describe the same
    // sun, or every scene that keeps its default silently changes size.
    const float degrees = DefaultOf<float>( sky, "SunAngularDiameter" );
    const float radians = degrees * 3.14159265358979323846f / 180.0f;
    EXPECT_NEAR( radians * 0.5f, 0.02f, 1e-5f );
}

// The one planet radius in the engine. It is authored in KILOMETRES — 6360 is a number a reviewer can
// check, 636000000 is not — so it must not carry Length, which means centimetres everywhere else in this
// codebase. If it ever silently becomes a world-unit field, the default changes by a factor of 100000 and
// the planet shell ends up inside the ground.
TEST( SkyAtmosphereReflection, PlanetRadiusIsKilometresAndConvertsToWorldUnits )
{
    const TypeInfo&  sky    = Type( "SkyAtmosphereData" );
    const FieldInfo* radius = Find( sky, "PlanetRadius" );
    ASSERT_NE( radius, nullptr );

    EXPECT_EQ( radius->Type, FieldType::Float );
    EXPECT_EQ( radius->Meta.Units, "km" );
    EXPECT_FALSE( radius->Meta.IsLength ) << "Length means centimetres; this field is kilometres";
    EXPECT_TRUE( radius->Meta.HasRange );
    EXPECT_FLOAT_EQ( radius->Meta.RangeMin, 1.0f );
    EXPECT_FLOAT_EQ( radius->Meta.RangeMax, 20000.0f );

    const float km = DefaultOf<float>( sky, "PlanetRadius" );
    EXPECT_FLOAT_EQ( km, 6360.0f );
    EXPECT_FLOAT_EQ( Common::Units::Metres( km * 1000.0f ), 636000000.0f );

    // The centre is derived, not authored: no second field may exist for it.
    EXPECT_EQ( Find( sky, "PlanetCenter" ), nullptr ) << "the centre is PlanetRadius below the origin";
}

TEST( SkyAtmosphereReflection, PresetAndResolutionAreEnumsWithEveryEnumerator )
{
    const TypeInfo& sky = Type( "SkyAtmosphereData" );

    const FieldInfo* preset = Find( sky, "ActivePreset" );
    ASSERT_NE( preset, nullptr );
    EXPECT_EQ( preset->Type, FieldType::Enum ) << "a free-text string could name a preset that never existed";
    EXPECT_EQ( preset->EnumValues.size(), 6u );
    EXPECT_EQ( preset->EnumValues.front().Name, "Custom" );
    EXPECT_TRUE( preset->Meta.ReadOnly ) << "the preset row reports, it does not author";

    const FieldInfo* res = Find( sky, "EnvironmentResolution" );
    ASSERT_NE( res, nullptr );
    EXPECT_EQ( res->Type, FieldType::Enum );
    EXPECT_EQ( res->EnumValues.size(), 3u );

    // The model switch: exactly the two models that exist, gradient first so 0 is the compatible default.
    const FieldInfo* model = Find( sky, "Model" );
    ASSERT_NE( model, nullptr );
    EXPECT_EQ( model->Type, FieldType::Enum );
    EXPECT_EQ( model->EnumValues.size(), 2u );
    EXPECT_EQ( model->EnumValues.front().Name, "ArtisticGradient" );
}

TEST( SkyAtmosphereReflection, TimeOfDayRowsAreGatedByTheirOwnSwitch )
{
    const TypeInfo& sky = Type( "SkyAtmosphereData" );
    for ( const char* name : { "TimeOfDay", "DayLengthSeconds", "Latitude", "NorthOffset" } )
    {
        const FieldInfo* f = Find( sky, name );
        ASSERT_NE( f, nullptr ) << name;
        EXPECT_EQ( f->Meta.EditCondition, "DriveSunFromTimeOfDay" ) << name;
    }
    EXPECT_EQ( Find( sky, "RebakeSunAngleThreshold" )->Meta.EditCondition, "AutoRebakeEnvironment" );
}

// SKY-35: the sky's sun numbers and the light's sun numbers are different physical quantities, and each
// says so where an artist reads it. An empty tooltip here is how the two get "unified" into darkness.
TEST( SkyAtmosphereReflection, BothSunPairsExplainTheSplit )
{
    const TypeInfo& sky   = Type( "SkyAtmosphereData" );
    const TypeInfo& light = Type( "DirectionalLightData" );

    for ( const char* name : { "SunIntensity", "SunColor" } )
    {
        const std::string tip = Find( sky, name )->Meta.Tooltip;
        EXPECT_NE( tip.find( "directional light" ), std::string::npos ) << name << ": " << tip;
    }
    for ( const char* name : { "Color", "Intensity" } )
    {
        const std::string tip = Find( light, name )->Meta.Tooltip;
        EXPECT_NE( tip.find( "Sky Atmosphere" ), std::string::npos ) << name << ": " << tip;
    }
}

// ---------------------------------------------------------------------------------------------------
// ExponentialHeightFogData — 14 fields in three groups, UE's UExponentialHeightFogComponent parameter
// for parameter (Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md section 3.2). The fog HEIGHT is deliberately
// absent: it is the entity's TransformComponent Y, as UE takes it from the component transform.
// ---------------------------------------------------------------------------------------------------

TEST( HeightFogReflection, ExposesExactlyTheSpecifiedFieldsInOrder )
{
    const std::vector<std::string> expected = {
         "Enabled",
         "FogDensity",
         "FogHeightFalloff",
         "FogInscatteringLuminance",
         "SkyAtmosphereAmbientContributionColorScale",
         "FogMaxOpacity",
         "StartDistance",
         "FogCutoffDistance",
         "SecondFogDensity",
         "SecondFogHeightFalloff",
         "SecondFogHeightOffset",
         "DirectionalInscatteringExponent",
         "DirectionalInscatteringStartDistance",
         "DirectionalInscatteringLuminance",
    };

    const TypeInfo& fog = Type( "ExponentialHeightFogData" );
    EXPECT_EQ( fog.Fields.size(), 14u );
    EXPECT_EQ( FieldNames( fog ), expected );

    EXPECT_EQ( CountInCategory( fog, "Exponential Height Fog" ), 8u );
    EXPECT_EQ( CountInCategory( fog, "Second Fog Layer" ), 3u );
    EXPECT_EQ( CountInCategory( fog, "Directional Inscattering" ), 3u );

    // The fog height is NOT a field — one owner, the transform, or the fog floor and the entity that
    // owns it can disagree.
    EXPECT_EQ( Find( fog, "FogHeight" ), nullptr );
    EXPECT_EQ( Find( fog, "FogHeightOffset" ), nullptr );
}

// UE's authored defaults, pinned so a UE-calibrated fog transplants number for number: FogDensity 0.02,
// FogHeightFalloff 0.2, second layer off (density 0), MaxOpacity 1, StartDistance 0, cutoff off,
// exponent 4 with a 10000-unit (100 m) start, directional colour black (the sun supplies it).
TEST( HeightFogReflection, DefaultsAreUEs )
{
    const TypeInfo& fog = Type( "ExponentialHeightFogData" );

    EXPECT_TRUE( DefaultOf<bool>( fog, "Enabled" ) );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "FogDensity" ), 0.02f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "FogHeightFalloff" ), 0.2f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "FogMaxOpacity" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "StartDistance" ), 0.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "FogCutoffDistance" ), 0.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "SecondFogDensity" ), 0.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "SecondFogHeightFalloff" ), 0.2f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "SecondFogHeightOffset" ), 0.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "DirectionalInscatteringExponent" ), 4.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( fog, "DirectionalInscatteringStartDistance" ), 10000.0f );
    EXPECT_EQ( DefaultOf<glm::vec3>( fog, "DirectionalInscatteringLuminance" ), glm::vec3( 0.0f ) );
    EXPECT_EQ( DefaultOf<glm::vec3>( fog, "SkyAtmosphereAmbientContributionColorScale" ), glm::vec3( 1.0f ) );

    for ( const char* name : { "FogInscatteringLuminance", "SkyAtmosphereAmbientContributionColorScale",
                               "DirectionalInscatteringLuminance" } )
    {
        const FieldInfo* f = Find( fog, name );
        ASSERT_NE( f, nullptr ) << name;
        EXPECT_EQ( f->Type, FieldType::Vec3 ) << name;
        EXPECT_TRUE( f->Meta.IsColor ) << name << " must draw as a colour picker";
    }
}

// One world unit is one centimetre: every fog distance says so with Length, while the density and the
// falloffs deliberately do NOT — they keep UE's own "per 1000 cm" semantics (converted once, in
// PackFogParams) so UE numbers transplant unchanged, and marking them as lengths would be a lie.
TEST( HeightFogReflection, DistancesAreLengthsAndEveryFieldIsAnnotatedWellEnoughToAuthor )
{
    const TypeInfo& fog = Type( "ExponentialHeightFogData" );

    for ( const char* name : { "StartDistance", "FogCutoffDistance", "SecondFogHeightOffset",
                               "DirectionalInscatteringStartDistance" } )
        EXPECT_TRUE( Find( fog, name )->Meta.IsLength ) << name;

    for ( const char* name : { "FogDensity", "FogHeightFalloff", "SecondFogDensity", "SecondFogHeightFalloff",
                               "FogMaxOpacity", "DirectionalInscatteringExponent" } )
        EXPECT_FALSE( Find( fog, name )->Meta.IsLength ) << name;

    for ( const auto& f : fog.Fields )
    {
        EXPECT_FALSE( f.Meta.Tooltip.empty() ) << f.Name << " has no tooltip";
        EXPECT_FALSE( f.Meta.DisplayName.empty() ) << f.Name << " has no display name";
        if ( f.Type == FieldType::Float || f.Type == FieldType::Int )
            EXPECT_TRUE( f.Meta.HasRange ) << f.Name << " has no Range, so it draws as a bare drag field";
    }
}

// ---------------------------------------------------------------------------------------------------
// VolumetricCloudData — 32 fields in six groups. The layer geometry and the tracing limits are
// UVolumetricCloudComponent's name for name, so a UE-calibrated sky transplants number for number; the
// shape group is ours, because UE has no cloud-shape parameter on the component at all (its density is a
// material graph). Every one of them is packed into Graphic::CloudGpuPayload or into the noise bake key —
// SettingConsumers holds that half of the promise, this test holds the roster, the order and the defaults.
// ---------------------------------------------------------------------------------------------------

TEST( VolumetricCloudReflection, ExposesExactlyTheSpecifiedFieldsInOrder )
{
    const std::vector<std::string> expected = {
         "Enabled",
         "LayerBottomAltitude",
         "LayerThickness",
         "PlanetRadius",
         "MaxViewDistance",
         "TracingStartMaxDistance",
         "TracingStartDistance",
         "Coverage",
         "CoverageContrast",
         "CloudType",
         "CloudTypeVariance",
         "WeatherTileSize",
         "WeatherSeed",
         "WeatherOctaves",
         "DetailTileSize",
         "DetailStrength",
         "DetailSeed",
         "DetailOctaves",
         "DensityScale",
         "ExtinctionScale",
         "NearFadeStartDistance",
         "NearFadeEndDistance",
         "ScatteringAlbedo",
         "PhaseG",
         "PhaseGBackward",
         "PhaseBlend",
         "AmbientOcclusionStrength",
         "LightMarchDistance",
         "LightMarchSamples",
         "MultiScatterOctaves",
         "MultiScatterContribution",
         "MultiScatterOcclusion",
         "MultiScatterEccentricity",
         "AerialPerspectiveStartDistance",
         "AerialPerspectiveFadeDistance",
         "AmbientScale",
         "MaxSteps",
         "StopTransmittance",
         "WindDirection",
         "WindSpeed",
    };

    const TypeInfo& cloud = Type( "VolumetricCloudData" );
    EXPECT_EQ( cloud.Fields.size(), 40u );
    EXPECT_EQ( FieldNames( cloud ), expected );

    EXPECT_EQ( CountInCategory( cloud, "Cloud Layer" ), 7u );
    EXPECT_EQ( CountInCategory( cloud, "Weather" ), 7u );
    EXPECT_EQ( CountInCategory( cloud, "Detail" ), 8u );
    EXPECT_EQ( CountInCategory( cloud, "Lighting" ), 14u );
    EXPECT_EQ( CountInCategory( cloud, "Quality" ), 2u );
    EXPECT_EQ( CountInCategory( cloud, "Animation" ), 2u );

    // The seeds and the octave counts belong to the BAKE and are deliberately absent from the march's
    // parameter block (Common/CloudParams.glslh says so). They are still component fields, because the
    // artist authors them — this pins that they did not migrate into some second home.
    for ( const char* onTheComponent : { "WeatherSeed", "WeatherOctaves", "DetailSeed", "DetailOctaves" } )
        EXPECT_NE( Find( cloud, onTheComponent ), nullptr ) << onTheComponent;

    // The wind OFFSET is not a field: it is state the ECS system integrates against the timestep. A
    // second copy on the component is a value that can disagree with the one the packer is handed.
    EXPECT_EQ( Find( cloud, "WindOffset" ), nullptr );
}

// The authored defaults, each of which was argued for in the component's own comments and several of
// which were corrected against a rendered frame. Pinned so that a change to any of them is a reviewable
// edit rather than a sky that quietly became a different sky.
TEST( VolumetricCloudReflection, DefaultsAreTheOnesTheComponentArguesFor )
{
    const TypeInfo& cloud = Type( "VolumetricCloudData" );

    EXPECT_TRUE( DefaultOf<bool>( cloud, "Enabled" ) );

    // Layer: UE's shipped 5 km base and 10 km envelope, with UE's own 6360 km planet. The envelope is a
    // CEILING and not a cloud — the vertical profile confines a stratocumulus to six per cent of it,
    // which Desert/Tests/Engine/CloudField asserts directly.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "LayerBottomAltitude" ), 500000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "LayerThickness" ), 1000000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PlanetRadius" ), 6360.0f );
    // 60 km, half of the calibrated pair; the relation the pair exists for is asserted separately below.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "MaxViewDistance" ), 6000000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "TracingStartDistance" ), 0.0f );

    // Weather: the coverage default is a MEASURED point inside the slider's useful band, not a taste.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "Coverage" ), 0.25f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "CoverageContrast" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "CloudType" ), 0.6f );
    // NON-ZERO ON PURPOSE: at zero every cloud in the layer reaches the same altitude, because the
    // vertical profile is then the same function everywhere, and the layer reads as a slab with a lid.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "CloudTypeVariance" ), 0.4f );
    // 12 km, the other half of the calibrated pair.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "WeatherTileSize" ), 1200000.0f );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "WeatherSeed" ), 1337 );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "WeatherOctaves" ), 3 );

    // Detail: the erosion is an order of magnitude weaker than the shape it cuts into, which is UE's own
    // ratio (base noise 0.8 against detail 0.08). At 0.5 it removed most of the layer and left a veil.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DetailTileSize" ), 400000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DetailStrength" ), 0.1f );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "DetailSeed" ), 13 );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "DetailOctaves" ), 2 );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DensityScale" ), 1.0f );
    // The EFFECTIVE extinction of a three-octave approximation, not the ~45/km of real cloud: at the
    // physical value every scattering order arrives at zero and the cloud renders uniformly grey.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "ExtinctionScale" ), 8.0f );

    // Lighting: UE's Cloud_AlbedoColor, and a forward-scattering phase.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "ScatteringAlbedo" ), 0.98f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseG" ), 0.8f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseGBackward" ), 0.1667f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseBlend" ), 0.575f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "AmbientOcclusionStrength" ), 0.5f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "TracingStartMaxDistance" ), 35000000.0f );
    // FIFTEEN kilometres, not the five hundred metres this line used to assert, and the change was
    // forced by a measurement rather than chosen: a shadow ray that starts inside a two-kilometre cloud
    // and is only 500 m long never leaves it, so every sample in the body reads the same optical depth
    // and the body shades flat. Docs/Clouds/CALIBRATION.md holds the frame it was found in; Unreal's
    // own ShadowTracingDistance is the same 15 km.
    //
    // Six samples rather than four is not six times the cost of a longer ray either — they are placed
    // on a squared distribution, so the first few still land in the metres nearest the sample and the
    // extra length is covered by the tail.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "LightMarchDistance" ), 1500000.0f );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "LightMarchSamples" ), 6 );
    // THREE, not one. A cloud lit by single scattering alone is physically grey; what makes a real one
    // white is light that has bounced inside it many times.
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "MultiScatterOctaves" ), 3 );
    // UE's shipped Multiscatter_Controls, channel for channel. The occlusion is the one that matters:
    // at 0.5 each successive order was absorbed twice as hard as it should be, light never reached the
    // core, and the cloud read grey rather than white.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "MultiScatterContribution" ), 0.667f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "MultiScatterOcclusion" ), 0.25f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "MultiScatterEccentricity" ), 0.18f );
    EXPECT_EQ( DefaultOf<glm::vec3>( cloud, "AmbientScale" ), glm::vec3( 1.0f ) );

    // Quality: a ceiling rather than a fixed cost, affordable only because the march spends a coarse step
    // on empty sky.
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "MaxSteps" ), 256 );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "StopTransmittance" ), 0.005f );

    // Animation: 30 m/s along +X.
    EXPECT_EQ( DefaultOf<glm::vec3>( cloud, "WindDirection" ), glm::vec3( 1.0f, 0.0f, 0.0f ) );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "WindSpeed" ), 3000.0f );
}

// A RELATION BETWEEN TWO DEFAULTS, and the one that Docs/Clouds/CALIBRATION.md section 4 was written
// about. Neither number is wrong on its own — 50 km of view distance is a perfectly ordinary setting and
// so is an 8 km weather tile — and that is exactly why this needs a test rather than two pinned values.
//
// THE QUANTITY THAT HAS TO BE BOUNDED IS THE NUMBER OF TILE REPEATS TO THE HORIZON: MaxViewDistance
// divided by WeatherTileSize. The coverage field is periodic with a period of one tile, so a ray bundle
// running out to the view distance crosses that many copies of the same field, and copies seen end-on
// toward the vanishing point read as streaks radiating from it. That is the radial moire section 4
// records, and the arithmetic reproduces its two data points exactly: the frame that showed the defect
// was 150 km against an 8 km tile — 18.75 repeats, which the document rounds to "about twenty times" —
// and the pair that cured it was 60 km against 12 km, five repeats.
//
// The ceiling is therefore the calibrated pair's own value and not a margin invented here. Five is the
// largest repeat count that has ever been LOOKED at and found clean.
TEST( VolumetricCloudReflection, TheWeatherTileRepeatsNoMoreOftenToTheHorizonThanTheCalibratedSkyDid )
{
    const TypeInfo& cloud = Type( "VolumetricCloudData" );

    constexpr float kCalibratedRepeats = 5.0f;

    const float viewDistance = DefaultOf<float>( cloud, "MaxViewDistance" );
    const float tileSize     = DefaultOf<float>( cloud, "WeatherTileSize" );

    ASSERT_GT( tileSize, 0.0f ) << "a tile of zero size makes the repeat count infinite";

    const float repeats = viewDistance / tileSize;

    EXPECT_LE( repeats, kCalibratedRepeats )
         << "the coverage field repeats " << repeats << " times between the camera and the vanishing point, "
         << "against the " << kCalibratedRepeats << " of the sky the calibration was measured on "
         << "(Docs/Clouds/CALIBRATION.md section 4). A new scene created from these defaults starts FURTHER "
         << "from the calibration than the scene the calibration was performed on.";

    // The other end. A tile so large relative to the view distance that the field never repeats at all is
    // not a moire problem, but it is the OTHER failure this pair produces — one cloud cell wider than the
    // whole visible field, which is how the zenith came out empty. One repeat is the floor.
    EXPECT_GE( repeats, 1.0f ) << "the visible field is smaller than one period of the coverage noise, so "
                                  "the whole sky is a single cell of it";
}

// THE NEAR FADE IS ONE SETTING SPELT WITH TWO FIELDS, and this is the relation between them.
//
// The march evaluates smoothstep(Start, End, t). GLSL leaves smoothstep UNDEFINED when Start >= End: past
// it the ratio is negative before the clamp on some implementations and not on others, and at equality it
// is a division by zero. Every OTHER repair in the packer is per-field and correct, and none of them can
// reach this, because Start = 5 km and End = 1 km are each inside their own slider and one edit apart in
// the Details panel.
//
// What is asserted is the RELATION on the packed block rather than the two numbers: whatever the artist
// authored, the pair that reaches the GPU either describes a real interval (End strictly past Start) or
// is switched off. Driving Graphic::PackCloudParams itself rather than the helper, because the block is
// what the shader reads and the .z/.w swap between the component and the payload is part of what could
// go wrong.
TEST( VolumetricCloudPayload, TheNearFadeReachesTheGpuAsAnIntervalOrNotAtAll )
{
    const Desert::Graphic::AtmosphereEnv atmosphere{};

    // Every combination of an authored Start and an authored End over the sliders' own range, including
    // the illegal orderings and the negatives a hand-edited scene file can carry.
    for ( const float startWorld : { -100000.0f, 0.0f, 1.0f, 50000.0f, 500000.0f, 2000000.0f } )
    {
        for ( const float endWorld : { -100000.0f, 0.0f, 1.0f, 50000.0f, 500000.0f, 2000000.0f } )
        {
            Desert::ECS::VolumetricCloudData data;
            data.NearFadeStartDistance = startWorld;
            data.NearFadeEndDistance   = endWorld;

            const Desert::Graphic::CloudGpuPayload payload =
                 Desert::Graphic::PackCloudParams( data, atmosphere, glm::vec3( 0.0f ) );

            const float endKm   = payload.Fade.z;
            const float startKm = payload.Fade.w;

            const bool off      = endKm == 0.0f && startKm == 0.0f;
            const bool interval = endKm > startKm;

            EXPECT_TRUE( off || interval ) << "Start " << startWorld << " cm, End " << endWorld
                                           << " cm packed to start " << startKm << " km, end " << endKm
                                           << " km — smoothstep(start, end, t) is undefined in GLSL unless "
                                              "end is strictly past start";

            // The gate the shader actually uses is `end > 0`, so "off" and "no interval" have to be the
            // SAME state or the gate lets a degenerate pair through.
            EXPECT_EQ( endKm > 0.0f, interval )
                 << "Start " << startWorld << " cm, End " << endWorld
                 << " cm: the shader's gate disagrees with whether there is an interval";

            // Neither end may be negative — a fade that begins behind the camera is not a fade.
            EXPECT_GE( startKm, 0.0f );
            EXPECT_GE( endKm, 0.0f );
        }
    }
}

TEST( VolumetricCloudPayload, ALegalNearFadeSurvivesThePackerUnchangedAndInKilometres )
{
    // The other half of the relation: switching a contradictory pair off must not be achieved by
    // switching every pair off. A legal interval arrives as itself, converted once, in the .z/.w order
    // the block documents.
    const Desert::Graphic::AtmosphereEnv atmosphere{};

    Desert::ECS::VolumetricCloudData data;
    data.NearFadeStartDistance = 100000.0f; // 1 km
    data.NearFadeEndDistance   = 500000.0f; // 5 km

    const Desert::Graphic::CloudGpuPayload payload =
         Desert::Graphic::PackCloudParams( data, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_FLOAT_EQ( payload.Fade.w, 1.0f );
    EXPECT_FLOAT_EQ( payload.Fade.z, 5.0f );

    // And a start of zero is a legal fade rather than an off one: it is UE's own default reading of
    // "apply it in full from the camera".
    data.NearFadeStartDistance = 0.0f;
    const Desert::Graphic::CloudGpuPayload fromCamera =
         Desert::Graphic::PackCloudParams( data, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_FLOAT_EQ( fromCamera.Fade.w, 0.0f );
    EXPECT_FLOAT_EQ( fromCamera.Fade.z, 5.0f );
}

TEST( VolumetricCloudReflection, EveryDefaultLiesInsideItsOwnRange )
{
    // A RELATION, and one that is easy to break by editing a default and not its slider: a value the
    // Details panel clamps away the moment the row is touched is a value the artist can never get back,
    // and the only symptom is that the sky changes when somebody drags a control and lets go.
    const TypeInfo& cloud = Type( "VolumetricCloudData" );

    for ( const auto& f : cloud.Fields )
    {
        if ( !f.Meta.HasRange )
            continue;

        const double value = f.Type == FieldType::Int
                                  ? static_cast<double>( DefaultOf<int32_t>( cloud, f.Name.c_str() ) )
                                  : static_cast<double>( DefaultOf<float>( cloud, f.Name.c_str() ) );

        EXPECT_GE( value, f.Meta.RangeMin ) << f.Name << " defaults below its own slider";
        EXPECT_LE( value, f.Meta.RangeMax ) << f.Name << " defaults above its own slider";
    }
}

// One world unit is one centimetre, and every distance on this component says so with Length — except
// the planet radius, which is authored in kilometres because 6360 km is 636 000 000 cm and no slider is
// useful at that scale. UE authors it the same way and for the same reason.
TEST( VolumetricCloudReflection, DistancesAreLengthsExceptTheTwoThatCarryTheirOwnUnit )
{
    const TypeInfo& cloud = Type( "VolumetricCloudData" );

    for ( const char* name : { "LayerBottomAltitude", "LayerThickness", "MaxViewDistance", "TracingStartDistance",
                               "WeatherTileSize", "DetailTileSize", "LightMarchDistance", "WindSpeed" } )
        EXPECT_TRUE( Find( cloud, name )->Meta.IsLength ) << name;

    // The two that are NOT world units, and say which units they are instead. Marking either as a length
    // would be a lie, and the editor would convert it.
    EXPECT_FALSE( Find( cloud, "PlanetRadius" )->Meta.IsLength );
    EXPECT_EQ( Find( cloud, "PlanetRadius" )->Meta.Units, "km" );
    EXPECT_FALSE( Find( cloud, "ExtinctionScale" )->Meta.IsLength );
    EXPECT_EQ( Find( cloud, "ExtinctionScale" )->Meta.Units, "/km" );

    // The dimensionless ones stay dimensionless.
    for ( const char* name : { "Coverage", "CoverageContrast", "CloudType", "CloudTypeVariance", "DetailStrength",
                               "DensityScale", "ScatteringAlbedo", "PhaseG", "StopTransmittance" } )
        EXPECT_FALSE( Find( cloud, name )->Meta.IsLength ) << name;

    EXPECT_TRUE( Find( cloud, "AmbientScale" )->Meta.IsColor ) << "the ambient scale must draw as a colour";
    EXPECT_EQ( Find( cloud, "AmbientScale" )->Type, FieldType::Vec3 );
    EXPECT_EQ( Find( cloud, "WindDirection" )->Type, FieldType::Vec3 );

    for ( const auto& f : cloud.Fields )
    {
        EXPECT_FALSE( f.Meta.Tooltip.empty() ) << f.Name << " has no tooltip";
        EXPECT_FALSE( f.Meta.DisplayName.empty() ) << f.Name << " has no display name";
        if ( f.Type == FieldType::Float || f.Type == FieldType::Int )
            EXPECT_TRUE( f.Meta.HasRange ) << f.Name << " has no Range, so it draws as a bare drag field";
    }
}

// ---------------------------------------------------------------------------------------------------
// What moved OUT of SkyboxComponent, and the two fields the directional light gained
// ---------------------------------------------------------------------------------------------------

TEST( SkyboxReflection, KeepsOnlyTheHdrCubemapPath )
{
    const TypeInfo& skybox = Type( "SkyboxComponent" );
    EXPECT_EQ( FieldNames( skybox ), ( std::vector<std::string>{ "SkyboxHandle", "Intensity" } ) );

    // The old path is deleted, not deprecated: a field left behind here is a second place a value can
    // live, and one of the two would never be tested again.
    for ( const char* gone : { "Procedural", "SunIntensity", "SunDiskRadius", "ZenithColor", "HorizonColor",
                               "GroundColor", "NightColor", "SkyBrightness", "HorizonFalloff", "SunColor",
                               "SunGlow", "SunsetColor", "SunsetIntensity", "StarIntensity" } )
        EXPECT_EQ( Find( skybox, gone ), nullptr ) << gone << " still lives on SkyboxComponent";
}

TEST( DirectionalLightReflection, GainsTheAtmosphereSunFields )
{
    // The UE-parity slice: the atmosphere-sun marker, the transmittance coupling and the Light Shafts
    // category, UE's names and defaults verbatim. Consumers: SkyboxECSSystem picks the sun, and
    // LightShaftRenderer reads the shafts via the SunLightFx slice of the ProceduralSkyCommand.
    const TypeInfo& light = Type( "DirectionalLightData" );
    EXPECT_EQ( FieldNames( light ),
               ( std::vector<std::string>{ "Color", "Intensity", "AtmosphereSunLight", "AtmosphereSunLightIndex",
                                           "AffectedByAtmosphereTransmittance", "LightShaftBloom", "BloomScale",
                                           "BloomThreshold", "BloomMaxBrightness", "BloomTint" } ) );

    // Sky Phase 4's coupling, UE's name and UE's default: ON. The light's colour is multiplied by the
    // atmosphere's transmittance toward the sun at ground level in SkyModel::PhysicalAtmosphere, so
    // sunsets redden the light on geometry; switching it off returns the authored colour. Consumer:
    // SceneRenderer::OnUpdate through AtmosphereEnv::SunTransmittanceAtGround.
    //
    // Default TRUE also means a scene saved before this field existed gains the coupling on load — which
    // is correct for the physical model (there is no such thing as an atmosphere that does not absorb)
    // and invisible on the artistic gradient, where the coupling does not exist at all.
    EXPECT_TRUE( DefaultOf<bool>( light, "AffectedByAtmosphereTransmittance" ) );

    // The Light Shafts group ships with UE's own defaults: OFF, and harmless when switched on.
    EXPECT_FALSE( DefaultOf<bool>( light, "LightShaftBloom" ) );
    EXPECT_FLOAT_EQ( DefaultOf<float>( light, "BloomScale" ), 0.2f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( light, "BloomThreshold" ), 0.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( light, "BloomMaxBrightness" ), 100.0f );

    const FieldInfo* marked = Find( light, "AtmosphereSunLight" );
    ASSERT_NE( marked, nullptr );
    EXPECT_EQ( marked->Type, FieldType::Bool );
    // True by default so that a scene saved before this field existed keeps its sky: a missing field keeps
    // the C++ default, so its one sun becomes the atmosphere sun with no migration.
    EXPECT_TRUE( DefaultOf<bool>( light, "AtmosphereSunLight" ) );

    const FieldInfo* index = Find( light, "AtmosphereSunLightIndex" );
    ASSERT_NE( index, nullptr );
    EXPECT_EQ( index->Type, FieldType::Int );
    EXPECT_TRUE( index->Meta.HasRange );
    // Pinned to 0: the engine renders exactly one directional light, and a slider that does nothing at
    // index 1 would be a dead setting.
    EXPECT_FLOAT_EQ( index->Meta.RangeMin, 0.0f );
    EXPECT_FLOAT_EQ( index->Meta.RangeMax, 0.0f );
    EXPECT_EQ( index->Meta.EditCondition, "AtmosphereSunLight" );
}

// ---------------------------------------------------------------------------------------------------
// A grey-out condition that names a field which is not a bool of the same block never fires: the row
// stays editable and nobody finds out. Check every reflected type, not only the new ones.
// ---------------------------------------------------------------------------------------------------

TEST( ReflectionMetadata, EveryEditConditionNamesABoolOfItsOwnBlock )
{
    for ( const auto& [name, type] : ReflectionRegistry::Get().All() )
    {
        for ( const auto& f : type.Fields )
        {
            if ( f.Meta.EditCondition.empty() )
                continue;

            std::string target = f.Meta.EditCondition;
            if ( target.front() == '!' )
                target.erase( target.begin() );

            const FieldInfo* gate = Find( type, target.c_str() );
            ASSERT_NE( gate, nullptr ) << name << "::" << f.Name << " is gated by a field that does not "
                                       << "exist: " << target;
            EXPECT_EQ( gate->Type, FieldType::Bool )
                 << name << "::" << f.Name << " is gated by non-bool " << target;
        }
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
