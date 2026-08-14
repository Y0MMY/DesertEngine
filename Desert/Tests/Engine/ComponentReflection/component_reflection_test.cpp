// What the sky and cloud components actually EXPOSE, checked against the specification field by field.
//
// The Details panel, scene serialization, undo, duplicate, prefabs and the Lua bindings are all driven by
// one table: the reflection metadata DesertHeaderTool generates from REFLECT()/PROPERTY(). A field that is
// mistyped, mis-categorised, or simply never annotated does not fail to compile — it silently does not
// exist. That is exactly the failure this test exists to catch, and it is checkable without a GPU: the
// generated translation unit is compiled straight into this binary and its static initializers fill the
// registry before main().

#include <Common/Core/Units.hpp>

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
// SkyAtmosphereData — 46 fields: the 24 artistic-gradient fields in their original order, then the 22
// physical-atmosphere fields (UE parameter names and grouping, Docs/Sky/UE_SKYATMOSPHERE_RESEARCH.md
// section 1.7), appended so the migration counters and the Details order both stay stable
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
    };

    const TypeInfo& sky = Type( "SkyAtmosphereData" );
    EXPECT_EQ( sky.Fields.size(), 46u );
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
    EXPECT_EQ( CountInCategory( sky, "Art Direction" ), 4u );

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
// the cloud shell ends up inside the ground.
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
// VolumetricCloudData — 99 fields in eight groups
// ---------------------------------------------------------------------------------------------------

TEST( VolumetricCloudReflection, ExposesNinetyNineFieldsInTheSpecifiedGroups )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );
    EXPECT_EQ( clouds.Fields.size(), 99u );

    EXPECT_EQ( CountInCategory( clouds, "Cloud Layer" ), 6u );
    EXPECT_EQ( CountInCategory( clouds, "Weather" ), 10u );
    EXPECT_EQ( CountInCategory( clouds, "Shape" ), 11u );
    EXPECT_EQ( CountInCategory( clouds, "Detail" ), 22u );
    EXPECT_EQ( CountInCategory( clouds, "Lighting" ), 25u );
    EXPECT_EQ( CountInCategory( clouds, "Animation" ), 8u );
    // 16 since the shadow map landed: Cloud Shadow Map and Cloud Shadow Extent.
    EXPECT_EQ( CountInCategory( clouds, "Quality" ), 16u );
    EXPECT_EQ( CountInCategory( clouds, "Preset" ), 1u );
}

// A field with no Tooltip is a number nobody outside this repository can interpret; a Float/Int with no
// Range silently becomes an unbounded DragFloat instead of a slider.
TEST( VolumetricCloudReflection, EveryFieldIsAnnotatedWellEnoughToAuthor )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );
    for ( const auto& f : clouds.Fields )
    {
        EXPECT_FALSE( f.Meta.Tooltip.empty() ) << f.Name << " has no tooltip";
        EXPECT_FALSE( f.Meta.DisplayName.empty() ) << f.Name << " has no display name";
        if ( f.Type == FieldType::Float || f.Type == FieldType::Int )
            EXPECT_TRUE( f.Meta.HasRange ) << f.Name << " has no Range, so it draws as a bare drag field";
    }
}

// One world unit is one centimetre everywhere. A distance that forgets to say so drags in the wrong
// magnitude and reads without a unit.
TEST( VolumetricCloudReflection, EveryQuantityWithAUnitDeclaresIt )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );
    for ( const auto& f : clouds.Fields )
    {
        const bool quantity = EndsWith( f.Name, "Altitude" ) || EndsWith( f.Name, "Thickness" ) ||
                              EndsWith( f.Name, "Distance" ) || EndsWith( f.Name, "Size" ) ||
                              EndsWith( f.Name, "Speed" );
        if ( quantity )
            EXPECT_TRUE( f.Meta.IsLength || !f.Meta.Units.empty() ) << f.Name << " states no unit";
    }
}

TEST( VolumetricCloudReflection, LayerGeometryIsInCentimetresWithMetreScaleDefaults )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );

    EXPECT_TRUE( Find( clouds, "LayerBottomAltitude" )->Meta.IsLength );
    EXPECT_TRUE( Find( clouds, "LayerThickness" )->Meta.IsLength );

    // 1500 m and 3500 m, in the engine's centimetres.
    EXPECT_FLOAT_EQ( DefaultOf<float>( clouds, "LayerBottomAltitude" ), 150000.0f );
    EXPECT_FLOAT_EQ( DefaultOf<float>( clouds, "LayerThickness" ), 350000.0f );
}

TEST( VolumetricCloudReflection, TheMasterSwitchGatesEveryOtherRow )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );
    for ( const auto& f : clouds.Fields )
    {
        if ( f.Name == "Enabled" )
        {
            EXPECT_TRUE( f.Meta.EditCondition.empty() ) << "the master switch cannot gate itself";
            continue;
        }
        // The Quality group is a performance tier: it stays editable while the layer is off, so a project
        // can dial cost down without switching the clouds on first.
        if ( f.Meta.Category == "Quality" || f.Meta.Category == "Preset" )
            continue;
        EXPECT_EQ( f.Meta.EditCondition, "Enabled" ) << f.Name << " is not gated by the master switch";
    }
}

TEST( VolumetricCloudReflection, QualityAndPresetSelectorsAreEnums )
{
    const TypeInfo& clouds = Type( "VolumetricCloudData" );

    const struct
    {
        const char* Field;
        std::size_t Enumerators;
    } cases[] = {
         { "QualityLevel", 5u },
         { "ResolutionScale", 3u },
         { "TemporalMode", 2u },
         { "Preset", 9u }, // Custom + eight presets, Summer Cumulus included
    };

    for ( const auto& c : cases )
    {
        const FieldInfo* f = Find( clouds, c.Field );
        ASSERT_NE( f, nullptr ) << c.Field;
        EXPECT_EQ( f->Type, FieldType::Enum ) << c.Field;
        EXPECT_EQ( f->EnumValues.size(), c.Enumerators ) << c.Field;
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
    for ( const char* gone :
          { "Procedural",    "SunIntensity", "SunDiskRadius",   "ZenithColor",     "HorizonColor",
            "GroundColor",   "NightColor",   "SkyBrightness",   "HorizonFalloff",  "SunColor",
            "SunGlow",       "SunsetColor",  "SunsetIntensity", "StarIntensity",   "EnableClouds",
            "CloudCoverage", "CloudDensity", "CloudTiling",     "CloudBrightness", "CloudWindSpeed" } )
        EXPECT_EQ( Find( skybox, gone ), nullptr ) << gone << " still lives on SkyboxComponent";
}

TEST( DirectionalLightReflection, GainsTheAtmosphereSunFields )
{
    // The UE-parity slice added 2026-08-14: Cloud Scattered Luminance Scale (the clouds' per-light sun
    // multiplier — UE reserves Volumetric Scattering Intensity for fog) and the Light Shafts category,
    // UE's names and defaults verbatim. Consumers: SkyboxRenderer (cloud scale) and LightShaftRenderer
    // via the SunLightFx slice of the ProceduralSkyCommand.
    const TypeInfo& light = Type( "DirectionalLightData" );
    EXPECT_EQ( FieldNames( light ),
               ( std::vector<std::string>{ "Color", "Intensity", "AtmosphereSunLight", "AtmosphereSunLightIndex",
                                           "CloudScatteredLuminanceScale", "LightShaftBloom", "BloomScale",
                                           "BloomThreshold", "BloomMaxBrightness", "BloomTint" } ) );

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
