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
#include <Engine/Assets/CloudTypeData.hpp>
#include <Engine/Graphic/Clouds/CloudPayload.hpp>
#include <Engine/Graphic/Clouds/CloudQuality.hpp>
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
// VolumetricCloudData — 38 fields in seven groups. The layer geometry and the tracing limits are
// UVolumetricCloudComponent's name for name, so a UE-calibrated sky transplants number for number; the
// shape group is ours, because UE has no cloud-shape parameter on the component at all (its density is a
// material graph). Every scalar is packed into Graphic::CloudGpuPayload and the one asset field names the
// CLOUD TYPE, which carries both the twelve numbers the shell and the profile are built from and the noise
// volume the edge is cut from — SettingConsumers holds that half of the promise, this test holds the
// roster, the order and the defaults.
// ---------------------------------------------------------------------------------------------------

TEST( VolumetricCloudReflection, ExposesExactlyTheSpecifiedFieldsInOrder )
{
    const std::vector<std::string> expected = {
         "Enabled",
         "CloudType1",
         "CloudType2",
         "CloudType3",
         "CloudType4",
         "PlanetRadius",
         "MaxViewDistance",
         "TracingStartMaxDistance",
         "TracingStartDistance",
         "Coverage",
         "CoverageContrast",
         "WeatherTileSize",
         "RegionSize",
         "Seed",
         "PlacementDensity",
         "PlacementScatter",
         "PlacementSizeVariety",
         "PatchTileSize",
         "PatchStrength",
         // Layout — the PAINTED sky. Six fields rather than one, because a painting has to be placed as
         // well as bound: an asset slot on its own would put an artist's picture over the world at one
         // scale, one orientation and one position, none of which they chose.
         "CloudLayout",
         "LayoutPatternStrength",
         "LayoutMaskStrength",
         "LayoutRepeats",
         "LayoutRotation",
         "LayoutOffset",
         "DetailTileSize",
         "DetailStrength",
         "DensityScale",
         "ExtinctionScale",
         "NearFadeStartDistance",
         "NearFadeEndDistance",
         "ScatteringAlbedo",
         "PhaseG",
         "PhaseGBackward",
         "PhaseBlend",
         "AmbientOcclusionStrength",
         "SkyOcclusionVolume",
         "PerSampleAtmosphereTransmittance",
         "LightMarchDistance",
         "LightMarchSamples",
         "MultiScatterOctaves",
         "MultiScatterContribution",
         "MultiScatterOcclusion",
         "MultiScatterEccentricity",
         "AerialPerspectiveStartDistance",
         "AerialPerspectiveFadeDistance",
         "AmbientScale",
         "CastShadows",
         "ShadowStrength",
         "MaxSteps",
         "StopTransmittance",
         "WindDirection",
         "WindSpeed",
    };

    const TypeInfo& cloud = Type( "VolumetricCloudData" );
    EXPECT_EQ( cloud.Fields.size(), 53u );
    EXPECT_EQ( FieldNames( cloud ), expected );

    EXPECT_EQ( CountInCategory( cloud, "Cloud Layer" ), 9u );
    EXPECT_EQ( CountInCategory( cloud, "Weather" ), 5u );
    // The five that decide whether the sky reads as a grid. See CALIBRATION.md section RW: the placement
    // that shipped before them put a measurable lattice bump at every multiple of the weather tile's
    // quarter, and each of these attacks one reason for it.
    EXPECT_EQ( CountInCategory( cloud, "Placement" ), 5u );
    // The painted layout: one slot and five numbers that place the painting in the world. Its own group
    // rather than more rows under Placement, because the two answer different questions — Placement is how
    // the ENGINE arranges clouds when nobody has said, and Layout is what happens when somebody has.
    EXPECT_EQ( CountInCategory( cloud, "Layout" ), 6u );
    // NO "Noise" GROUP ANY MORE: it held one row, the noise volume slot, and that moved onto the cloud
    // type. A group with nothing in it is a heading an artist opens and finds empty.
    EXPECT_EQ( CountInCategory( cloud, "Noise" ), 0u );
    EXPECT_EQ( CountInCategory( cloud, "Detail" ), 6u );
    // FIFTEEN SINCE Р4, and the row that arrived is Sky Occlusion Volume — a bool, and deliberately the
    // ONLY field that feature added. What it chooses is which geometry AmbientOcclusionStrength measures,
    // so the strength stayed one knob with one meaning; a second strength beside it would have been a
    // parameter whose only job is to say the same thing twice.
    //
    // SIXTEEN SINCE Р14, and the row that arrived is Per Sample Atmosphere Transmittance — again a bool
    // and again the only field its feature added. It chooses WHERE the atmosphere's cut is taken, not how
    // much of it: there is no strength beside it, because a physical transmittance scaled by taste is a
    // knob that hides the calibration rather than a parameter.
    EXPECT_EQ( CountInCategory( cloud, "Lighting" ), 16u );
    // THE SHADOWS GROUP IS TWO ROWS AND NOT FOUR. The map's extent and resolution are engine constants
    // like the step schedule (they trade cost against quality identically in every scene, and the extent
    // is DERIVED from the march's own resolvable chord). The sky-light occlusion under a deck is a
    // different quantity with a different geometry, and it now has its own volume — but it belongs to
    // LIGHTING, not here: what it occludes is the sky's ambient, not the sun, and nothing about it reaches
    // the shadow map on the ground.
    EXPECT_EQ( CountInCategory( cloud, "Shadows" ), 2u );
    EXPECT_EQ( CountInCategory( cloud, "Quality" ), 2u );
    EXPECT_EQ( CountInCategory( cloud, "Animation" ), 2u );

    // THE FOUR BAKE SETTINGS ARE GONE, and this is where that is pinned. WeatherSeed, WeatherOctaves,
    // DetailSeed and DetailOctaves parameterised a GPU bake that no longer exists; the seed and the
    // lattice periods that make a volume live in the volume asset's own header now, and the component
    // names the volume instead. A field that came back here would be a knob that rebakes nothing.
    for ( const char* gone : { "WeatherSeed", "WeatherOctaves", "DetailSeed", "DetailOctaves" } )
        EXPECT_EQ( Find( cloud, gone ), nullptr ) << gone << " is a bake setting and the bake is gone";

    // AND SO ARE THE THREE THE PROFILE TABLE REPLACED. LayerBottomAltitude and LayerThickness stated by
    // hand a shell that is now computed from the type's own altitudes — two numbers obliged to agree with
    // a third, which is the §2.3.1 defect class — and CloudTypeVariance mixed noise into one analytic
    // profile curve, which is now a per-type table indexed by the placement pattern. A field that came
    // back here would be an authored value contradicting a computed one.
    //
    // (There IS a "CloudType" now, and it is the asset handle below rather than the old scalar. The name
    // was reused deliberately: it is what the thing has always been called in the UI, and the v3 -> v4
    // migration had already deleted every occurrence of the scalar before v4 -> v5 wrote the handle.)
    for ( const char* gone : { "LayerBottomAltitude", "LayerThickness", "CloudTypeVariance" } )
        EXPECT_EQ( Find( cloud, gone ), nullptr )
             << gone << " was replaced by the cloud type and must not have a second life";

    // AND THE NOISE VOLUME SLOT IS GONE FROM HERE, because it moved onto the cloud type: the character of
    // an edge is a property of the KIND of cloud. A slot that came back would be a second source of truth
    // for one thing (§4.2), and the two would disagree the first time an artist set only one of them.
    EXPECT_EQ( Find( cloud, "NoiseVolume" ), nullptr )
         << "the noise volume is a field of the cloud type now, not of the layer";

    // FOUR SLOTS AND NOT ONE, and the singular name is gone rather than kept as the first of them. A
    // `CloudType` standing beside `CloudType2` would read as one field of a different kind next to three
    // of another; the v5 -> v6 migration renames it, and this is what stops it coming back.
    EXPECT_EQ( Find( cloud, "CloudType" ), nullptr )
         << "the single cloud type slot became a set of four and must not have a second life";

    for ( const char* slot : { "CloudType1", "CloudType2", "CloudType3", "CloudType4" } )
    {
        const FieldInfo* type = Find( cloud, slot );
        ASSERT_NE( type, nullptr ) << slot;
        EXPECT_TRUE( type->Meta.IsAsset ) << slot;
        EXPECT_EQ( type->Meta.AssetType, "CloudTypeAsset" ) << slot;
    }

    // AND THERE ARE EXACTLY AS MANY AS THE PROFILE TABLE HAS CHANNELS. The ceiling is the width of a
    // texel, so a fifth slot here would be a slot with nowhere to put its profile — and the failure would
    // be a species that silently never appears rather than an error.
    EXPECT_EQ( Desert::Graphic::kCloudSpeciesSlots, 4u );

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

    // Layer: THE SHELL IS NOT AUTHORED. It is computed from the species' own altitudes by
    // Graphic::PackCloudParams, and the relation `envelope contains the species` is asserted further
    // down on the packed block. What is left here is the planet the shell curves around — UE's own
    // 6360 km — and the species itself.
    // AN EMPTY SLOT, which is the documented "use the engine's built-in cumulus congestus". A scene that
    // names no type — and a scene created from these defaults names none — must still have a sky, and
    // that requirement is why the empty handle has a meaning rather than being a hole. The default is NOT
    // the id of a shipped file, which would make every new scene depend on a file being present.
    for ( const char* slot : { "CloudType1", "CloudType2", "CloudType3", "CloudType4" } )
        EXPECT_EQ( DefaultOf<uint64_t>( cloud, slot ), 0u ) << slot;
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PlanetRadius" ), 6360.0f );
    // 60 km, half of the calibrated pair; the relation the pair exists for is asserted separately below.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "MaxViewDistance" ), 6000000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "TracingStartDistance" ), 0.0f );

    // Weather: the coverage default is a MEASURED point inside the slider's useful band, not a taste. It
    // has moved twice, each time to keep the SKY the same while the field under it changed — to 0.15 when
    // the coverage field became a quantile rather than a level, and to 0.10 when the envelope stopped
    // being an authored ten kilometres and became the species' own three-and-a-half. Both numbers come
    // from the table Desert/Tests/Engine/CloudField prints.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "Coverage" ), 0.45f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "CoverageContrast" ), 1.0f );
    // AND NOTHING ELSE ABOUT THE SHAPE IS AUTHORED HERE. The species decides the altitudes and the profile
    // table decides the silhouette at each of them. The domain warp that briefly stood between the two was
    // measured and removed; VolumetricCloudComponent.hpp records with what numbers.
    EXPECT_EQ( Find( cloud, "ShapeDistortion" ), nullptr );
    // 12 km, the other half of the calibrated pair.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "WeatherTileSize" ), 1200000.0f );

    // Detail: BOTH of these are relations rather than tastes, and both were re-measured after phase Э5
    // moved the producer under them (Desert/Tests/Engine/CloudField owns the measurements and asserts the
    // relations; what is pinned here is only that the shipped numbers are the ones it measured).
    //
    // The tile is one kilometre because the erosion's wave has to be SHORTER than a cloud — at the four
    // kilometres this used to carry, one wave spanned 0.83 of a body and scaled it instead of texturing
    // it — and LONGER than the march's 125 m resolvable chord, which half a kilometre already is not.
    //
    // The strength is the smallest value with REAL headroom over the floor that the cut must clear: it has
    // to move the surface the eye sees by more than the 125 m the march can find, or the erosion carves
    // structure finer than the renderer represents — and above that floor every step costs cloud for a gain
    // nothing has measured.
    //
    // 0.65 AND NOT §DS'S 0.40, AND THE REASON IS NOT IN THIS COMPONENT. This number is one half of a pair
    // whose other half is Assets::kCloudLumpVerticalOverHorizontal, the shape of the lump a body is built
    // from. §SIL2 raised that from 0.45 to 0.75 so a cloud reads as a body rather than a plate; a taller
    // lump is optically thicker per metre, so the SAME cut moves the visible surface a shorter distance,
    // and at §DS's 0.40 the travel fell to 101 m — under the floor. 0.65 restores it to 139 m, which is
    // §DS's own 1.11x to the metre. Docs/Clouds/CALIBRATION.md §SIL2 carries the ladder and the frames, and
    // Desert/Tests/Engine/CloudField asserts the PAIR rather than either number.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DetailTileSize" ), 100000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DetailStrength" ), 0.65f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "DensityScale" ), 1.0f );
    // The EFFECTIVE extinction of a three-octave approximation, not the ~45/km of real cloud: at the
    // physical value every scattering order arrives at zero and the cloud renders uniformly grey.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "ExtinctionScale" ), 8.0f );

    // Lighting: UE's Cloud_AlbedoColor, and a forward-scattering phase.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "ScatteringAlbedo" ), 0.98f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseG" ), 0.8f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseGBackward" ), 0.1667f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "PhaseBlend" ), 0.575f );
    // THE OCCLUSION IS ONE CALIBRATION AND THESE ARE ITS TWO HALVES, so they are asserted together and
    // the message names the other one. The strength is 1.0 and not UE's 0.5 because Р7's composition
    // halves what any strength buys — `1 - s(1 - T)` became `1 - (s/2)(1 - T)` — so 1.0 against the
    // VOLUME is the same amount of occlusion UE's 0.5 describes, and it is the setting Р4 measured as
    // closing 34 % and 29 % of the contrast gap at the two horizon points. Against the PROFILE term the
    // same 1.0 means something else entirely: the local occluder at its ceiling, which Р0 measured at
    // 17 % there. A default pair that drifted apart would therefore not be two settings slightly wrong,
    // it would be a sky nobody chose — §2.3.1's "two values obliged to agree".
    EXPECT_TRUE( DefaultOf<bool>( cloud, "SkyOcclusionVolume" ) )
         << "the sky-light occlusion volume is off by default again, but AmbientOcclusionStrength's "
            "default of 1.0 is calibrated for the volume's geometry, not the profile term's";
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "AmbientOcclusionStrength" ), 1.0f )
         << "the strength no longer matches the geometry SkyOcclusionVolume's default selects";
    // OFF, WHICH IS UNREAL'S DEFAULT for the same field and the reason the whole calibration below is
    // still readable: every number this programme has measured was measured against the sun colour
    // `OuterSpaceIlluminance x T(ground)`, and turning this on replaces that colour everywhere in the
    // shell at once. A default of true would silently re-base CALIBRATION.md.
    EXPECT_FALSE( DefaultOf<bool>( cloud, "PerSampleAtmosphereTransmittance" ) )
         << "the per-sample atmospheric sun transmittance is on by default, which moves every frame this "
            "programme has calibrated against and is not what Unreal ships";
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "TracingStartMaxDistance" ), 35000000.0f );
    // FIFTEEN kilometres, not the five hundred metres this line used to assert, and the change was
    // forced by a measurement rather than chosen: a shadow ray that starts inside a two-kilometre cloud
    // and is only 500 m long never leaves it, so every sample in the body reads the same optical depth
    // and the body shades flat. Docs/Clouds/CALIBRATION.md holds the frame it was found in; Unreal's
    // own ShadowTracingDistance is the same 15 km.
    //
    // AND THE SAMPLE COUNT HAD TO MOVE WITH THE LENGTH, which is what the paragraph that used to stand
    // here got wrong. It said a longer ray costs almost nothing because the squared distribution keeps
    // the first samples near the shaded point. It does not: on a squared distribution the FIRST segment
    // is the march length over the SQUARE of the count, so lengthening the ray from 500 m to 15 km at a
    // fixed six samples coarsened the near field from 13.9 m to 417 m — by exactly the factor it
    // lengthened the ray. Thirty-two is where the rendered sunward highlight stops moving; see
    // Docs/Clouds/CALIBRATION.md section OE-FIX for the convergence table and the price.
    EXPECT_FLOAT_EQ( DefaultOf<float>( cloud, "LightMarchDistance" ), 1500000.0f );
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "LightMarchSamples" ), 32 );
    // The slider must be able to REACH the value that converges, and the old ceiling of sixteen could
    // not — sixteen renders the sunward zenith 34% too bright in linear radiance. The top is the shared
    // constant, so this assertion is about the relation and not about the number: if somebody lowers the
    // ceiling below the default, the range test further down catches it too.
    EXPECT_FLOAT_EQ( Find( cloud, "LightMarchSamples" )->Meta.RangeMax,
                     static_cast<float>( Desert::ECS::kCloudLightMarchMaxSamples ) );
    EXPECT_GE( Desert::ECS::kCloudLightMarchMaxSamples, DefaultOf<int32_t>( cloud, "LightMarchSamples" ) );
    // THREE, not one. A cloud lit by single scattering alone is physically grey; what makes a real one
    // white is light that has bounced inside it many times.
    EXPECT_EQ( DefaultOf<int32_t>( cloud, "MultiScatterOctaves" ), 3 );
    // And the same relation the shadow ray's ceiling has, for the same reason: the Range, the payload's
    // clamp and the clamp in Common/CloudLighting.glslh are three copies of one number. Р18 made them
    // one constant while measuring whether more octaves buy anything — they do not, and the constant
    // carries the measurement.
    EXPECT_FLOAT_EQ( Find( cloud, "MultiScatterOctaves" )->Meta.RangeMax,
                     static_cast<float>( Desert::ECS::kCloudMultiScatterMaxOctaves ) );
    EXPECT_GE( Desert::ECS::kCloudMultiScatterMaxOctaves, DefaultOf<int32_t>( cloud, "MultiScatterOctaves" ) );
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

            const Desert::Graphic::CloudGpuPayload payload = Desert::Graphic::PackCloudParams(
                 data, &Desert::Assets::CloudTypeDefaultShape(), 1u, atmosphere, glm::vec3( 0.0f ) );

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

    const Desert::Graphic::CloudGpuPayload payload = Desert::Graphic::PackCloudParams(
         data, &Desert::Assets::CloudTypeDefaultShape(), 1u, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_FLOAT_EQ( payload.Fade.w, 1.0f );
    EXPECT_FLOAT_EQ( payload.Fade.z, 5.0f );

    // And a start of zero is a legal fade rather than an off one: it is UE's own default reading of
    // "apply it in full from the camera".
    data.NearFadeStartDistance = 0.0f;
    const Desert::Graphic::CloudGpuPayload fromCamera = Desert::Graphic::PackCloudParams(
         data, &Desert::Assets::CloudTypeDefaultShape(), 1u, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_FLOAT_EQ( fromCamera.Fade.w, 0.0f );
    EXPECT_FLOAT_EQ( fromCamera.Fade.z, 5.0f );
}

// THE SUN THE BLOCK CARRIES AND THE TRANSMITTANCE EACH MARCH APPLIES ARE ONE DECISION SPELT THREE TIMES.
//
// Graphic::PackCloudParams chooses WHICH illuminance goes on the wire; VolumetricCloudRenderer chooses
// whether to raise CloudPush::Frame.y so the screen march multiplies by T(sample), and whether to tell
// the environment bake the same through CloudEnvironmentBake::PerSampleSunTransmittance. If those ever
// disagree the frame is not slightly wrong — it is the outer-space illuminance with no atmosphere applied
// to it at all, which at a low sun is several times the light the scene is exposed for.
//
// They cannot disagree because Graphic::CloudUsesPerSampleSunTransmittance answers for all three, and what
// is asserted here is that the packer really does defer to it — over every combination of the flag and the
// two handles whose presence the answer depends on.
TEST( VolumetricCloudPayload, TheSunColourAndThePerSampleGateAgreeOnEveryCombination )
{
    // Opaque handles: AtmosphereEnv holds them as forward-declared pointers and the packer only ever
    // tests them against null, so a distinct non-null address is a complete stand-in for a real image.
    auto* const lut      = reinterpret_cast<Desert::Graphic::Image2D*>( 0x1000 );
    auto* const skyLight = reinterpret_cast<Desert::Graphic::Image2D*>( 0x2000 );

    // Three visibly different sun quantities, so an assertion below cannot pass by two of them colliding.
    const glm::vec3 outerSpace( 8.0f, 7.0f, 6.0f );
    const glm::vec3 onGround( 4.0f, 2.0f, 1.0f ); // outerSpace x a plausible low-sun transmittance
    const glm::vec3 skyDisc( 0.5f, 0.4f, 0.3f );

    for ( const bool flag : { false, true } )
    {
        for ( const bool haveLut : { false, true } )
        {
            for ( const bool haveSkyLight : { false, true } )
            {
                for ( const bool valid : { false, true } )
                {
                    Desert::Graphic::AtmosphereEnv atmosphere{};
                    atmosphere.Valid                    = valid;
                    atmosphere.SunOuterSpaceIlluminance = outerSpace;
                    atmosphere.SunIlluminanceOnGround   = onGround;
                    atmosphere.SunIrradiance            = skyDisc;
                    atmosphere.TransmittanceLut         = haveLut ? lut : nullptr;
                    atmosphere.DistantSkyLight          = haveSkyLight ? skyLight : nullptr;

                    Desert::ECS::VolumetricCloudData data;
                    data.PerSampleAtmosphereTransmittance = flag;

                    const bool perSample = Desert::Graphic::CloudUsesPerSampleSunTransmittance( data, atmosphere );

                    // The gate may only fire when all four conditions hold — that is the renderers'
                    // contract for binding the real LUT rather than the fallback texture.
                    EXPECT_EQ( perSample, flag && valid && haveLut && haveSkyLight );

                    const Desert::Graphic::CloudGpuPayload payload = Desert::Graphic::PackCloudParams(
                         data, &Desert::Assets::CloudTypeDefaultShape(), 1u, atmosphere, glm::vec3( 0.0f ) );

                    const glm::vec3 packed( payload.SunColour );

                    // WHICH sun the block carries, in the same three cases the packer distinguishes. An
                    // invalid atmosphere is black on every path, which is the "no sky component" state.
                    const glm::vec3 expected = !valid         ? glm::vec3( 0.0f )
                                               : perSample    ? outerSpace
                                               : haveSkyLight ? onGround
                                                              : skyDisc;

                    EXPECT_FLOAT_EQ( packed.x, expected.x )
                         << "flag " << flag << ", lut " << haveLut << ", skyLight " << haveSkyLight << ", valid "
                         << valid;
                    EXPECT_FLOAT_EQ( packed.y, expected.y );
                    EXPECT_FLOAT_EQ( packed.z, expected.z );

                    // AND THE ONE THAT MATTERS MOST: the outer-space illuminance may reach the GPU ONLY
                    // when a march is going to apply a transmittance to it. Stated separately from the
                    // table above because this is the failure that is dangerous rather than merely wrong.
                    if ( packed == outerSpace && outerSpace != onGround )
                        EXPECT_TRUE( perSample )
                             << "the block carries the sun's colour before the atmosphere while the march "
                                "is told to apply no atmosphere to it";
                }
            }
        }
    }
}

TEST( VolumetricCloudPayload, TheEnvelopeContainsEveryTypeItIsBuiltFrom )
{
    // THE RELATION THAT REPLACED TWO AUTHORED FIELDS. The shell the march intersects used to be
    // Layer Bottom Altitude and Layer Thickness, and a cloud type's altitudes used to be two more numbers
    // that had to agree with them — the §2.3.1 shape of defect exactly: each side individually legal, the
    // disagreement visible only as a cumulonimbus whose anvil has been sliced off by a ceiling nobody
    // remembers setting.
    //
    // The shell is now COMPUTED, and this is the statement that it is computed correctly: whatever type a
    // layer names, the block the shader reads describes a shell that contains all of that type and no more
    // of the sky than it needs.
    const Desert::Graphic::AtmosphereEnv atmosphere{};

    // FIXTURES RATHER THAN THE SHIPPED LIBRARY, and deliberately extreme ones: what is under test is the
    // packer, and it must hold for any twelve numbers an artist can save — including a type thinner than
    // any in the library and one whose second lobe sits far above its tower. The library's own numbers are
    // Desert/Tests/Engine/CloudType's business.
    struct Fixture
    {
        const char*                     Name;
        Desert::Graphic::CloudTypeShape Shape;
    };

    const Fixture fixtures[] = {
         { "a sheet on the ground",
           { 0.15f, 0.55f, 0.88f, 0.12f, 0.35f, 0.0f, 0.0f, 0.0f, 0.05f, 0.5f, 0.70f, 0.75f, 1.0f, 1.0f } },
         { "a fair-weather heap",
           { 0.90f, 1.90f, 0.45f, 0.06f, 0.45f, 0.0f, 0.0f, 0.0f, 0.70f, 1.0f, 1.00f, 1.00f, 1.0f, 1.0f } },
         { "the built-in default", Desert::Assets::CloudTypeDefaultShape() },
         { "a storm with an anvil",
           { 0.90f, 9.00f, 0.12f, 0.04f, 0.40f, 9.5f, 1.8f, 0.85f, 0.85f, 1.0f, 1.35f, 1.30f, 1.0f, 1.0f } },
         { "a wisp two hundred metres thick, high up",
           { 8.00f, 8.20f, 0.90f, 0.25f, 0.55f, 0.0f, 0.0f, 0.0f, 0.00f, 2.5f, 0.35f, 0.25f, 1.0f, 1.0f } },
    };

    // EVERY SUBSET OF THE FIXTURES UP TO THE CEILING, and not just each of them alone. The relation is
    // now over a SET — T3 made the shell the union of up to four bands — and a union is exactly the kind
    // of computation that is right for one member and wrong for two. The subsets are enumerated as bit
    // patterns so nothing is left out by hand.
    constexpr int kFixtureCount = static_cast<int>( std::size( fixtures ) );

    for ( int mask = 1; mask < ( 1 << kFixtureCount ); ++mask )
    {
        Desert::Graphic::CloudTypeShape set[Desert::Graphic::kCloudSpeciesSlots]{};
        const char*                     names[Desert::Graphic::kCloudSpeciesSlots]{};

        uint32_t count = 0;
        for ( int i = 0; i < kFixtureCount && count < Desert::Graphic::kCloudSpeciesSlots; ++i )
        {
            if ( ( mask & ( 1 << i ) ) == 0 )
                continue;
            names[count] = fixtures[i].Name;
            set[count]   = fixtures[i].Shape;
            ++count;
        }

        Desert::ECS::VolumetricCloudData data;

        const Desert::Graphic::CloudGpuPayload payload =
             Desert::Graphic::PackCloudParams( data, set, count, atmosphere, glm::vec3( 0.0f ) );

        const float bottomKm = payload.Layer.y;
        const float topKm    = payload.Layer.y + payload.Layer.z;

        float tightestBottom = 1e9f;
        float tightestTop    = -1e9f;

        for ( uint32_t slot = 0; slot < count; ++slot )
        {
            const float typeBottomKm = Desert::Graphic::CloudTypeBaseKm( set[slot] );
            const float typeTopKm    = Desert::Graphic::CloudTypeTopKm( set[slot] );

            EXPECT_LE( bottomKm, typeBottomKm ) << names[slot] << " has its base below the shell";
            EXPECT_GE( topKm, typeTopKm ) << names[slot] << " has its top above the shell";

            tightestBottom = std::min( tightestBottom, typeBottomKm );
            tightestTop    = std::max( tightestTop, typeTopKm );

            // The anvil is ABOVE the tower, so a top taken from TopAltitudeKm alone would cut it off.
            // Stated separately because it is the one case in which the two are not the same number.
            if ( set[slot].AnvilStrength > 0.0f )
                EXPECT_GE( topKm, set[slot].AnvilAltitudeKm + set[slot].AnvilThicknessKm )
                     << "the anvil is outside the shell";
        }

        // AND THE SHELL IS NOT LARGER THAN IT NEEDS TO BE. A generous envelope satisfies the containment
        // above trivially — ten kilometres contains every type in the library — while charging every
        // ray for the empty air it has to march through. Tight in both directions is the property, and
        // over a set "tight" means the lowest base and the highest top of the MEMBERS.
        EXPECT_NEAR( bottomKm, tightestBottom, 1e-4f );
        EXPECT_NEAR( topKm, tightestTop, 1e-3f );
    }
}

TEST( VolumetricCloudPayload, AnEmptySetPacksAShellThatDrawsNothingRatherThanAGuess )
{
    // The renderer answers an all-empty layer with ONE built-in congestus, so a count of zero never
    // reaches the packer in the running engine. It is asserted anyway, because a packer that depends on
    // its caller having checked something is a packer that will one day be called by someone who did not:
    // what comes out is a legal, degenerate shell rather than a NaN thickness or a species count the
    // march would loop over.
    const Desert::Graphic::AtmosphereEnv atmosphere{};
    Desert::ECS::VolumetricCloudData     data;

    const Desert::Graphic::CloudGpuPayload payload =
         Desert::Graphic::PackCloudParams( data, nullptr, 0u, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_FLOAT_EQ( payload.Detail.w, 0.0f ) << "an empty set claims to have species in it";
    EXPECT_GT( payload.Layer.z, 0.0f ) << "a shell of zero thickness divides by zero in the step schedule";

    for ( uint32_t slot = 0; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
    {
        EXPECT_FLOAT_EQ( payload.SpeciesEdge[slot].z, 0.0f )
             << "an unfilled slot carries a density that could put cloud in the sky";
    }
}

TEST( VolumetricCloudPayload, TheTypesMatterAndEdgeReachTheGpuAsProductsOfTheLayersOwnScales )
{
    // WHAT A CLOUD TYPE IS MADE OF, and the one rule that keeps a type from fighting the layer it is in:
    // the type's three factors MULTIPLY the artist's three scales, and the product is formed once, here.
    // Two absolute values for one quantity would be two numbers that can disagree, and the symptom would
    // be an artist's Density Scale doing nothing because the type had already decided.
    const Desert::Graphic::AtmosphereEnv atmosphere{};

    // NAMED FIELD BY FIELD RATHER THAN BY POSITION, and the reason is a defect this file carried for one
    // build. The fifth slot used to be `TopTaper`; when Р2 replaced it with the profile CURVE, brace
    // elision quietly fed the taper's old value into `Profile.HalfWidth[0]` and shifted every number after
    // it one place up the array. It compiled in a `constexpr` context and produced shapes that drew NO
    // CLOUD, which is how three tests in this file went red at once. A positional initialiser of an
    // aggregate whose members can change is a silent field-shift waiting to happen.
    const Desert::Graphic::CloudTypeShape ice{ /* BaseAltitudeKm */ 8.00f,
                                               /* TopAltitudeKm */ 9.40f,
                                               /* EdgeTopFraction */ 0.90f,
                                               /* BaseRampFraction */ 0.25f,
                                               /* Profile */ Desert::Graphic::CloudProfileFromTaper( 0.55f ),
                                               /* AnvilAltitudeKm */ 0.0f,
                                               /* AnvilThicknessKm */ 0.0f,
                                               /* AnvilStrength */ 0.0f,
                                               /* DetailCharacter */ 0.00f,
                                               /* DetailFactor */ 2.50f,
                                               /* DensityFactor */ 0.35f,
                                               /* ExtinctionFactor */ 0.25f,
                                               /* PlacementScale */ 2.50f,
                                               /* PlacementAnisotropy */ 8.00f };
    const Desert::Graphic::CloudTypeShape storm{ /* BaseAltitudeKm */ 0.90f,
                                                 /* TopAltitudeKm */ 9.00f,
                                                 /* EdgeTopFraction */ 0.12f,
                                                 /* BaseRampFraction */ 0.04f,
                                                 /* Profile */ Desert::Graphic::CloudProfileFromTaper( 0.40f ),
                                                 /* AnvilAltitudeKm */ 9.5f,
                                                 /* AnvilThicknessKm */ 1.8f,
                                                 /* AnvilStrength */ 0.85f,
                                                 /* DetailCharacter */ 0.85f,
                                                 /* DetailFactor */ 1.00f,
                                                 /* DensityFactor */ 1.35f,
                                                 /* ExtinctionFactor */ 1.30f,
                                                 /* PlacementScale */ 2.00f,
                                                 /* PlacementAnisotropy */ 1.00f };

    for ( const Desert::Graphic::CloudTypeShape& shape : { ice, storm, Desert::Assets::CloudTypeDefaultShape() } )
    {
        Desert::ECS::VolumetricCloudData data;
        data.DensityScale    = 0.5f;
        data.ExtinctionScale = 8.0f;
        data.DetailStrength  = 0.2f;

        const Desert::Graphic::CloudGpuPayload payload =
             Desert::Graphic::PackCloudParams( data, &shape, 1u, atmosphere, glm::vec3( 0.0f ) );

        // THE PRODUCT IS NO LONGER FORMED HERE, and that is T3's one change to this relation. With four
        // kinds of cloud in one shell the march does not know which factor it needs until it knows which
        // species won the sample, so the layer's scale and the type's factor travel separately and the
        // multiply happens at the point of use. What is asserted is therefore that each half arrives
        // intact AND that their product is still the number the single-type build sent.
        EXPECT_FLOAT_EQ( payload.SpeciesEdge[0].x, shape.DetailCharacter );

        EXPECT_FLOAT_EQ( payload.Detail.y, 0.5f );
        EXPECT_FLOAT_EQ( payload.SpeciesEdge[0].z, shape.DensityFactor );
        EXPECT_FLOAT_EQ( payload.Detail.y * payload.SpeciesEdge[0].z, 0.5f * shape.DensityFactor );

        EXPECT_FLOAT_EQ( payload.March.w, 8.0f );
        EXPECT_FLOAT_EQ( payload.SpeciesEdge[0].w, shape.ExtinctionFactor );
        EXPECT_FLOAT_EQ( payload.March.w * payload.SpeciesEdge[0].w, 8.0f * shape.ExtinctionFactor );

        EXPECT_FLOAT_EQ( payload.Detail.x, 0.2f );
        EXPECT_FLOAT_EQ( payload.SpeciesEdge[0].y, shape.DetailFactor );

        // ONE SPECIES IN THE LAYER MEANS ONE IN THE COUNT, and the other three slots are ZERO rather than
        // the default type's numbers: an unfilled slot must not be able to put cloud in the sky even if
        // the count were ever wrong.
        EXPECT_FLOAT_EQ( payload.Detail.w, 1.0f );
        for ( uint32_t slot = 1; slot < Desert::Graphic::kCloudSpeciesSlots; ++slot )
        {
            EXPECT_FLOAT_EQ( payload.SpeciesEdge[slot].z, 0.0f ) << slot;
        }
    }

    // And the products SEPARATE the types: ice is thinner stuff than a storm in both of the two ways a
    // frame can show it. If this ever fails, the library has two names for one cloud.
    Desert::ECS::VolumetricCloudData layer;

    const auto icePayload   = Desert::Graphic::PackCloudParams( layer, &ice, 1u, atmosphere, glm::vec3( 0.0f ) );
    const auto stormPayload = Desert::Graphic::PackCloudParams( layer, &storm, 1u, atmosphere, glm::vec3( 0.0f ) );

    EXPECT_LT( icePayload.SpeciesEdge[0].z, stormPayload.SpeciesEdge[0].z );
    EXPECT_LT( icePayload.SpeciesEdge[0].w, stormPayload.SpeciesEdge[0].w );
}

TEST( VolumetricCloudPayload, TheNoiseVolumesAreDeduplicatedAndEverySlotStaysBindable )
{
    // THE MAPPING THE RENDERER BINDS BY AND THE PACKER NUMBERS BY, and the only reason it is a function
    // rather than two loops is that those two must not disagree: if the images were bound in one order
    // and indexed in another, every cloud in the sky would be eroded by the wrong volume with nothing in
    // the log. Pure, so this suite can drive it without a device.
    using Desert::Assets::AssetHandle;
    using Desert::Graphic::kCloudSpeciesSlots;
    using Desert::Graphic::ResolveCloudNoiseVolumes;

    const AssetHandle none = AssetHandle::Null();
    const AssetHandle fine{ 111u };
    const AssetHandle other{ 222u };

    // ONE VOLUME BETWEEN FOUR SPECIES — the state of every scene in the repository, because eight of the
    // nine shipped types name no volume of their own. It must resolve to ONE slot and to {0,0,0,0}: that
    // is what makes the march's four-way select uniform across the wave and therefore free.
    {
        const AssetHandle all[kCloudSpeciesSlots] = { none, none, none, none };
        const auto        resolved                = ResolveCloudNoiseVolumes( all, kCloudSpeciesSlots );

        EXPECT_EQ( resolved.DistinctCount, 1u );
        for ( uint32_t k = 0; k < kCloudSpeciesSlots; ++k )
        {
            EXPECT_EQ( resolved.SlotOfSpecies[k], 0u ) << k;
            EXPECT_EQ( resolved.Volume[k], none ) << "slot " << k << " is not bindable";
        }
    }

    // A NULL HANDLE IS A VALUE AND NOT A MISSING ONE. Two types that both name nothing share a slot for
    // the same reason two that name the same file do — otherwise the commonest layer in the project would
    // burn four descriptors and four branches on one image.
    {
        const AssetHandle mixed[kCloudSpeciesSlots] = { none, fine, none, fine };
        const auto        resolved                  = ResolveCloudNoiseVolumes( mixed, kCloudSpeciesSlots );

        EXPECT_EQ( resolved.DistinctCount, 2u );
        EXPECT_EQ( resolved.SlotOfSpecies[0], 0u );
        EXPECT_EQ( resolved.SlotOfSpecies[1], 1u );
        EXPECT_EQ( resolved.SlotOfSpecies[2], 0u );
        EXPECT_EQ( resolved.SlotOfSpecies[3], 1u );
        EXPECT_EQ( resolved.Volume[0], none );
        EXPECT_EQ( resolved.Volume[1], fine );
        // The two slots nobody asked for still hold a real image, which is the difference between an
        // unused descriptor and an INVALID descriptor set — and this backend answers an invalid set by
        // skipping the dispatch, so the clouds would vanish with nothing in the log.
        EXPECT_EQ( resolved.Volume[2], none );
        EXPECT_EQ( resolved.Volume[3], none );
    }

    // AND THE INDEX THE MARCH READS IS THE ONE THIS PRODUCED. The packer is the other half of the wire;
    // asserting the resolver alone would leave the two free to drift, which is the defect class this whole
    // function exists to close.
    {
        const AssetHandle mixed[kCloudSpeciesSlots] = { other, fine, other, none };
        const auto        resolved                  = ResolveCloudNoiseVolumes( mixed, kCloudSpeciesSlots );

        EXPECT_EQ( resolved.DistinctCount, 3u );

        Desert::Graphic::CloudTypeShape shapes[kCloudSpeciesSlots];
        for ( uint32_t k = 0; k < kCloudSpeciesSlots; ++k )
            shapes[k] = Desert::Assets::CloudTypeDefaultShape();

        Desert::ECS::VolumetricCloudData layer;
        Desert::Graphic::AtmosphereEnv   atmosphere;

        const auto payload = Desert::Graphic::PackCloudParams(
             layer, shapes, kCloudSpeciesSlots, atmosphere, glm::vec3( 0.0f ),
             Desert::Graphic::CloudRegionBinding{}, Desert::ECS::kCloudLightMarchMaxSamples, 0.0f, resolved );

        for ( uint32_t k = 0; k < kCloudSpeciesSlots; ++k )
        {
            EXPECT_FLOAT_EQ( payload.SpeciesNoise[static_cast<int>( k )],
                             static_cast<float>( resolved.SlotOfSpecies[k] ) )
                 << "species " << k
                 << " reaches the march pointing at a different volume from the one the "
                    "renderer bound for it";
        }
    }

    // A LAYER WITH NO SPECIES AT ALL still binds one volume, because the alternative is an invalid
    // descriptor set. The packer is written to be correct when its caller has checked nothing.
    {
        const auto resolved = ResolveCloudNoiseVolumes( nullptr, 0u );
        EXPECT_EQ( resolved.DistinctCount, 1u );
        for ( uint32_t k = 0; k < kCloudSpeciesSlots; ++k )
            EXPECT_EQ( resolved.SlotOfSpecies[k], 0u ) << k;
    }
}

// THE TEST THAT STOOD HERE WENT WITH THE THING IT MEASURED.
//
// `EachSpeciesGetsItsOwnPlacementFieldAndTheWindIsItsAxis` asserted the other half of D-14 on the block
// the shader reads: two types in one layer arriving with different basis vectors, the deck's field
// 1/0.35 times finer across the wind, stretched 1.6 times along it, and the whole frame turning when the
// wind did. There is no basis in the block any more — the lumps are laid out on a lattice in the wind's
// frame on the CPU at bake time (Engine/Assets/CloudProceduralVolume.cpp), so a basis the march does not
// read would be four dead vec4s.
//
// THE PROPERTY DID NOT GO WITH IT. It is asserted on the generator that now owns it, in
// Desert/Tests/Engine/CloudProceduralField — `EachSpeciesGetsItsOwnLatticeAndTheWindIsItsAxis` — where it
// is stronger: it measures the lumps' actual positions rather than the frequencies they would have been
// read at.

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

    for ( const char* name : { "MaxViewDistance", "TracingStartDistance", "WeatherTileSize", "DetailTileSize",
                               "LightMarchDistance", "WindSpeed" } )
        EXPECT_TRUE( Find( cloud, name )->Meta.IsLength ) << name;

    // The two that are NOT world units, and say which units they are instead. Marking either as a length
    // would be a lie, and the editor would convert it.
    EXPECT_FALSE( Find( cloud, "PlanetRadius" )->Meta.IsLength );
    EXPECT_EQ( Find( cloud, "PlanetRadius" )->Meta.Units, "km" );
    EXPECT_FALSE( Find( cloud, "ExtinctionScale" )->Meta.IsLength );
    EXPECT_EQ( Find( cloud, "ExtinctionScale" )->Meta.Units, "/km" );

    // The dimensionless ones stay dimensionless.
    for ( const char* name : { "Coverage", "CoverageContrast", "DetailStrength", "DensityScale",
                               "ScatteringAlbedo", "PhaseG", "StopTransmittance" } )
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

// ---------------------------------------------------------------------------------------------------
// THE QUALITY TIER, WHERE IT BECOMES A NUMBER THE SHADER RUNS
// ---------------------------------------------------------------------------------------------------
//
// The tier's own relations — the map's texel against the chord the march can find, the border fade, the
// coverage ladder — live in Desert/Tests/Engine/CloudShadow, which owns those. This is the other half:
// the packer is the ONE place an authored field becomes a GPU number, so this is where a tier that is
// only a comment would be caught.

namespace
{
    // Every enumerator, listed once. A tier missing from here is a tier nothing checks.
    constexpr Desert::Core::CloudQuality kAllQualityTiers[] = {
         Desert::Core::CloudQuality::Low,
         Desert::Core::CloudQuality::Medium,
         Desert::Core::CloudQuality::High,
    };

    const char* QualityTierName( Desert::Core::CloudQuality tier )
    {
        switch ( tier )
        {
            case Desert::Core::CloudQuality::Low:
                return "Low";
            case Desert::Core::CloudQuality::Medium:
                return "Medium";
            case Desert::Core::CloudQuality::High:
                return "High";
        }
        return "?";
    }

    // The component's default Max Steps, which no tier lowers.
    constexpr float kTierMaxSteps = 256.0f;
} // namespace

// THE TIER REACHING THE GPU BLOCK, which is where a tier that is only a comment would be caught. The
// packer is the one place the component's authored numbers become the numbers the shader runs, so this is
// the assertion that the ceiling is APPLIED and that nothing else moved with it.
TEST( CloudQualityTier, TheTierCapsTheShadowRayAndLeavesTheMarchAlone )
{
    Desert::ECS::VolumetricCloudData data;
    data.LightMarchSamples = 32; // the shipped default, and above Low's ceiling
    data.MaxSteps          = static_cast<int32_t>( kTierMaxSteps );

    Desert::Graphic::AtmosphereEnv atmosphere;
    atmosphere.Valid         = true;
    atmosphere.SunDirection  = glm::normalize( glm::vec3( 0.3f, 0.75f, 0.55f ) );
    atmosphere.SunIrradiance = glm::vec3( 20.0f );

    // The envelope this suite already works in, as a shape rather than a layer. Only the two altitudes
    // matter to the two fields asserted below, and stating them keeps this test off the asset loader.
    Desert::Graphic::CloudTypeShape shape{};
    shape.BaseAltitudeKm   = 2.2f;
    shape.TopAltitudeKm    = 5.8f;
    shape.PlacementScale   = 1.0f;
    shape.DensityFactor    = 1.0f;
    shape.ExtinctionFactor = 1.0f;

    for ( const Desert::Core::CloudQuality tier : kAllQualityTiers )
    {
        const Desert::Graphic::CloudQualityScale scale = Desert::Graphic::CloudQualityFor( tier );
        const Desert::Graphic::CloudGpuPayload   payload = Desert::Graphic::PackCloudParams(
             data, &shape, 1u, atmosphere, glm::vec3( 0.0f ), Desert::Graphic::CloudRegionBinding{},
             scale.LightMarchSampleCeiling, scale.StopTransmittanceFloor );

        // The shadow ray is min(authored, ceiling) — the tier lowers it and never raises it.
        EXPECT_FLOAT_EQ( payload.SunColour.w,
                         static_cast<float>( std::min( data.LightMarchSamples, scale.LightMarchSampleCeiling ) ) )
             << QualityTierName( tier ) << " did not apply its shadow-ray ceiling to the packed block";
        EXPECT_LE( payload.SunColour.w, static_cast<float>( data.LightMarchSamples ) )
             << QualityTierName( tier ) << " made the frame MORE expensive than the artist asked for";

        // AND MAX STEPS IS UNTOUCHED ON EVERY TIER. This is the executable half of the refusal:
        // Desert/Tests/Engine/CloudType measures that the shipped library tolerates Max Steps down to 233
        // against the component's 256, nine per cent, so there is nothing for a tier to spend here — and
        // if one ever tries, this line is what says so.
        EXPECT_FLOAT_EQ( payload.March.x, kTierMaxSteps )
             << QualityTierName( tier ) << " lowered Max Steps, which the shipped cloud library cannot afford";

        // The march's stop threshold composes the OTHER way — max(), because ending the march earlier is
        // what makes it cheaper — and the two compositions have to point the same way or a tier stops
        // being a budget and becomes a second opinion about the sky.
        EXPECT_FLOAT_EQ( payload.March.y, std::max( data.StopTransmittance, scale.StopTransmittanceFloor ) )
             << QualityTierName( tier ) << " did not apply its stop-transmittance floor";
        EXPECT_GE( payload.March.y, data.StopTransmittance )
             << QualityTierName( tier ) << " made the march run LONGER than the artist asked for";
    }

    // An artist who has already asked for the cheap answer keeps it on every tier — a ceiling is a
    // ceiling and a floor is a floor, so neither can pull an authored number back toward the expensive
    // side.
    data.LightMarchSamples = 8;
    data.StopTransmittance = 0.2f;
    for ( const Desert::Core::CloudQuality tier : kAllQualityTiers )
    {
        const Desert::Graphic::CloudQualityScale scale = Desert::Graphic::CloudQualityFor( tier );
        const Desert::Graphic::CloudGpuPayload   payload = Desert::Graphic::PackCloudParams(
             data, &shape, 1u, atmosphere, glm::vec3( 0.0f ), Desert::Graphic::CloudRegionBinding{},
             scale.LightMarchSampleCeiling, scale.StopTransmittanceFloor );
        EXPECT_FLOAT_EQ( payload.SunColour.w, 8.0f ) << QualityTierName( tier ) << " overruled an authored 8";
        EXPECT_FLOAT_EQ( payload.March.y, 0.2f ) << QualityTierName( tier ) << " overruled an authored 0.2";
    }
}

// ---------------------------------------------------------------------------------------------------
// The HERO CLOUD - slot A of the cloud field's seam
// ---------------------------------------------------------------------------------------------------
//
// A CENSUS AND NOT A SAMPLE, on the same terms as the layer's above: the roster, its order, the per-
// category counts and every default. What it prevents is the field that appears in the Details panel and
// reaches nothing, which is the defect this whole suite is about and which a component with SEVEN fields
// is exactly small enough for nobody to bother checking by hand.

TEST( HeroCloudReflection, ExposesExactlyTheSpecifiedFieldsInOrder )
{
    const std::vector<std::string> expected = {
         "Enabled",      "Volume",        "Strength",         "SuppressProceduralField",
         "DetailFactor", "DensityFactor", "ExtinctionFactor",
    };

    const TypeInfo& hero = Type( "HeroCloudData" );
    EXPECT_EQ( hero.Fields.size(), 7u );
    EXPECT_EQ( FieldNames( hero ), expected );

    EXPECT_EQ( CountInCategory( hero, "Hero Cloud" ), 4u );
    EXPECT_EQ( CountInCategory( hero, "Material" ), 3u );

    // THE THREE MATERIAL NUMBERS ARE THE CLOUD TYPE'S, NAME FOR NAME, and that is a relation rather than
    // a coincidence: what a cloud is MADE OF is a property of the cloud and not of which producer drew
    // it, so 1 has to mean the same thing on both sides of the seam. The FOURTH of that set, Detail
    // Character, is deliberately absent - the volume carries it per voxel, which is what lets a body's
    // wispy tail erode differently from its billowy core.
    EXPECT_NE( Find( hero, "DetailFactor" ), nullptr );
    EXPECT_NE( Find( hero, "DensityFactor" ), nullptr );
    EXPECT_NE( Find( hero, "ExtinctionFactor" ), nullptr );
    EXPECT_EQ( Find( hero, "DetailCharacter" ), nullptr );

    // AND THERE IS NO AUTHORED POSITION, SIZE OR ROTATION. Where a hero cloud is comes from the entity's
    // TransformComponent and its own size comes from the `.dcmv`; a field here would be a second
    // statement of one of them, which is the class of defect this programme has paid for most often.
    EXPECT_EQ( Find( hero, "Position" ), nullptr );
    EXPECT_EQ( Find( hero, "Altitude" ), nullptr );
    EXPECT_EQ( Find( hero, "SizeKm" ), nullptr );
    EXPECT_EQ( Find( hero, "Rotation" ), nullptr );
}

TEST( HeroCloudReflection, TheVolumeSlotIsAnAssetOfTheRightType )
{
    const FieldInfo* volume = Find( Type( "HeroCloudData" ), "Volume" );
    ASSERT_NE( volume, nullptr );

    EXPECT_EQ( volume->Type, FieldType::AssetHandle );
    EXPECT_TRUE( volume->Meta.IsAsset );
    // The string the Details panel dispatches on, and the one ComponentRegistry's resolver matches to
    // turn the handle into a relative path. Three sites, one spelling.
    EXPECT_EQ( volume->Meta.AssetType, "CloudModellingVolumeAsset" );
}

TEST( HeroCloudReflection, DefaultsAreTheOnesTheComponentArguesFor )
{
    const TypeInfo& hero = Type( "HeroCloudData" );

    // On by default, because a component somebody added is a component they want.
    EXPECT_TRUE( DefaultOf<bool>( hero, "Enabled" ) );

    // AND THE CUTOUT IS ON BY DEFAULT, which is the one default here that is an argument rather than an
    // obvious choice: without it a procedural blob grows through the sculpted body the moment it is
    // placed, and an artist meeting that on their first hero cloud would conclude the feature does not
    // work (ANALYSIS_APPROACH.md section 4.3).
    EXPECT_TRUE( DefaultOf<bool>( hero, "SuppressProceduralField" ) );

    // Full strength, and the three material factors at the identity - so a body dropped into a scene
    // renders as it was sculpted and as the layer was tuned, with nothing to discover.
    EXPECT_FLOAT_EQ( DefaultOf<float>( hero, "Strength" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( hero, "DetailFactor" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( hero, "DensityFactor" ), 1.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( hero, "ExtinctionFactor" ), 1.0f );
}

TEST( HeroCloudReflection, EveryFieldIsAnnotatedWellEnoughToDraw )
{
    const TypeInfo& hero = Type( "HeroCloudData" );

    for ( const auto& f : hero.Fields )
    {
        EXPECT_FALSE( f.Meta.Tooltip.empty() ) << f.Name << " has no tooltip";
        EXPECT_FALSE( f.Meta.DisplayName.empty() ) << f.Name << " has no display name";
        EXPECT_FALSE( f.Meta.Category.empty() ) << f.Name << " has no category";
        if ( f.Type == FieldType::Float || f.Type == FieldType::Int )
            EXPECT_TRUE( f.Meta.HasRange ) << f.Name << " has no Range, so it draws as a bare drag field";
    }
}

TEST( HeroCloudReflection, EveryRangedDefaultLiesInsideItsOwnRange )
{
    const TypeInfo& hero = Type( "HeroCloudData" );

    for ( const auto& f : hero.Fields )
    {
        if ( !f.Meta.HasRange || f.Type != FieldType::Float )
            continue;

        const float value = DefaultOf<float>( hero, f.Name.c_str() );
        EXPECT_GE( value, f.Meta.RangeMin ) << f.Name << " defaults below its own slider";
        EXPECT_LE( value, f.Meta.RangeMax ) << f.Name << " defaults above its own slider";
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
