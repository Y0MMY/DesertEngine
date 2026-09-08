// The cloud material SCHEMA census — DEV_CONTRACT §1.3's fifth link, restated for parameters that are no
// longer reflected C++.
//
// O1 moved thirty-three look fields off ECS::VolumetricCloudData into the Properties block of
// CloudRaymarch.shader (domain Volume). SettingConsumers pins every REFLECTED field to a consumer; the
// moment a value stops being reflected that guarantee lapses, and this suite is where it continues: the
// schema, the C++ mirror that consumes it (Graphic::CloudMaterialValues), and the defaults the two agree
// on are asserted as RELATIONS, in both directions, against the shader file the engine actually loads.
//
// WHY A MIRROR AT ALL, AND WHY IT IS NOT A SECOND SOURCE OF TRUTH. At runtime the schema supplies every
// default (BuildCloudMaterialValues reads the parsed Properties); the struct's member initializers exist
// so a missing shader degrades to the SAME sky loudly rather than to a zeroed one silently. That is only
// safe while the two are byte-equal — which is precisely what this suite pins, so a default retuned in
// the shader without the mirror (or vice versa) is a red test naming the parameter, not a sky that
// quietly forked from its fallback.
//
// Pure: reads one file, runs the engine's own parser, touches no GPU and no registry.

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>
#include <Engine/ECS/VolumetricCloudComponent.hpp>
#include <Engine/Graphic/Clouds/CloudMaterialValues.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using Desert::Core::Formats::ShaderDomain;
using Desert::Core::Formats::ShaderParam;
using Desert::Core::Formats::ShaderProgramMeta;
using Desert::Core::Preprocess::DShaderParser;
using Desert::Graphic::BuildCloudMaterialValues;
using Desert::Graphic::CloudMaterialValues;
using Desert::Graphic::MaterialOverrides;

namespace
{
    // Walks up from the working directory looking for a file only the repository has — the test runner's
    // working directory is not fixed. Same shape as CloudProtocolScene's RepoRoot, same reason.
    std::string RepoRoot()
    {
        std::string prefix = "./";
        for ( int up = 0; up < 6; ++up )
        {
            std::ifstream probe( prefix + "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" );
            if ( probe )
                return prefix;
            prefix += "../";
        }
        return {};
    }

    std::string ReadAll( const std::string& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    // Parsed ONCE for the whole suite: the file on disk is the thing under test, and every test reads
    // the same parse of it.
    const ShaderProgramMeta& Schema()
    {
        static const ShaderProgramMeta meta = []
        {
            const std::string path = RepoRoot() + "Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader";
            const std::string source = ReadAll( path );
            EXPECT_FALSE( source.empty() ) << path << " is unreadable";

            auto parsed = DShaderParser::Parse( source );
            EXPECT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
            return parsed.IsSuccess() ? parsed.GetValue().Meta : ShaderProgramMeta{};
        }();
        return meta;
    }

    const ShaderParam* Find( const char* name )
    {
        for ( const auto& p : Schema().Params )
            if ( p.Name == name )
                return &p;
        return nullptr;
    }

    // THE MOVED THIRTY-THREE, spelled once, with the storage each one mirrors. This table is the census:
    // both directions of the schema<->struct correspondence are asserted against it, so a parameter added
    // to either side alone is a red test naming the name.
    struct ValueRow
    {
        const char* Name;
        int         Components; // 1 scalar, 2/3 vector
        bool        IsInt;
    };
    constexpr ValueRow kValues[] = {
         { "Coverage", 1, false },
         { "CoverageContrast", 1, false },
         { "WeatherTileSize", 1, false },
         { "Seed", 1, true },
         { "PlacementDensity", 1, false },
         { "PlacementScatter", 1, false },
         { "PlacementSizeVariety", 1, false },
         { "PatchTileSize", 1, false },
         { "PatchStrength", 1, false },
         { "LayoutPatternStrength", 1, false },
         { "LayoutMaskStrength", 1, false },
         { "LayoutRepeats", 1, true },
         { "LayoutRotation", 1, true },
         { "LayoutOffset", 2, false },
         { "DetailTileSize", 1, false },
         { "DetailStrength", 1, false },
         { "DensityScale", 1, false },
         { "ExtinctionScale", 1, false },
         // THREE, since the Volume domain's output contract landed: the scattering albedo is the medium's
         // own colour. A `.demat` written while it was one is raised by
         // Migration::MigrateCloudMaterialAlbedoToColour, not papered over by the reader.
         { "ScatteringAlbedo", 3, false },
         { "PhaseG", 1, false },
         { "PhaseGBackward", 1, false },
         { "PhaseBlend", 1, false },
         { "AmbientOcclusionStrength", 1, false },
         { "MultiScatterOctaves", 1, true },
         { "MultiScatterContribution", 1, false },
         { "MultiScatterOcclusion", 1, false },
         { "MultiScatterEccentricity", 1, false },
         { "AmbientScale", 3, false },
    };
    struct AssetRow
    {
        const char* Name;
        const char* Kind;
    };
    constexpr AssetRow kAssets[] = {
         { "CloudType1", "CloudTypeAsset" },
         { "CloudType2", "CloudTypeAsset" },
         { "CloudType3", "CloudTypeAsset" },
         { "CloudType4", "CloudTypeAsset" },
         // TWO LAYOUT INPUTS AND NOT ONE (O-4), which is Unreal's own arrangement:
         // `Layout_CloudGlobalPattern` and `Layout_GlobalCloudMask` are separate texture parameters
         // there. A census that still named the single `CloudLayout` would pass on a shader that had
         // quietly lost the mask input.
         { "LayoutPattern", "CloudLayoutAsset" },
         { "LayoutMask", "CloudLayoutAsset" },
         // THE AUTHORED MEDIUM, and it is an asset reference rather than a value because what it names is
         // a body of CODE: a Volume-domain shader graph that replaces the density, extinction, albedo,
         // emission and occlusion of the medium itself. It reaches the frame through the shader compiler
         // — Core::ShaderVariant, substituted into the four programs that sample the cloud field — and
         // never through the packed parameter block, which is why it costs the march nothing per sample.
         // Null is the shipped medium.
         { "Medium", "ShaderAsset" },
    };
} // namespace

TEST( CloudMaterialSchema, TheShaderIsTheVolumeDomainAndItsNameIsTheSharedConstant )
{
    EXPECT_EQ( Schema().Domain, ShaderDomain::Volume )
         << "the cloud shader stopped declaring Domain Volume, so no material picker offers it and the "
            "cloud component's slot has nothing to point at";

    // The one spelling: the renderer's pipeline lookup, the schema resolve and the layout panel all use
    // this constant, and the file on disk has to be the program it names.
    EXPECT_STREQ( Desert::Graphic::kCloudMaterialShaderName, "CloudRaymarch" );
}

TEST( CloudMaterialSchema, TheSchemaAndTheMirrorAgreeInBothDirections )
{
    // Every table row exists in the schema, with the storage the mirror expects.
    for ( const ValueRow& row : kValues )
    {
        const ShaderParam* p = Find( row.Name );
        ASSERT_NE( p, nullptr ) << row.Name << " is in CloudMaterialValues but not in the schema — the "
                                << "material editor cannot author it";
        EXPECT_FALSE( p->IsTexture ) << row.Name;
        EXPECT_FALSE( p->IsAssetRef() ) << row.Name;
        using VT = Desert::Core::Formats::ShaderValueType;
        if ( row.IsInt )
            EXPECT_EQ( p->Type, VT::Int ) << row.Name;
        else if ( row.Components == 2 )
            EXPECT_EQ( p->Type, VT::Float2 ) << row.Name;
        else if ( row.Components == 3 )
            EXPECT_EQ( p->Type, VT::Float3 ) << row.Name;
        else
            EXPECT_EQ( p->Type, VT::Float ) << row.Name;
    }
    for ( const AssetRow& row : kAssets )
    {
        const ShaderParam* p = Find( row.Name );
        ASSERT_NE( p, nullptr ) << row.Name;
        EXPECT_EQ( p->AssetKind, row.Kind ) << row.Name;
    }

    // And the schema carries NOTHING the mirror does not read: a parameter here that no C++ consumes is
    // §1.3's dead setting, wearing the new clothes.
    std::set<std::string> known;
    for ( const ValueRow& row : kValues )
        known.insert( row.Name );
    for ( const AssetRow& row : kAssets )
        known.insert( row.Name );

    EXPECT_EQ( Schema().Params.size(), known.size() );
    for ( const auto& p : Schema().Params )
        EXPECT_TRUE( known.count( p.Name ) )
             << p.Name << " is in the schema but not in CloudMaterialValues — a knob nothing reads";
}

TEST( CloudMaterialSchema, TheSchemaDefaultsAreTheMirrorsToTheDigit )
{
    // The mirror at its member initializers IS the schema's defaults — asserted value by value rather
    // than through BuildCloudMaterialValues, so a divergence names the parameter instead of the pair.
    const CloudMaterialValues mirror{};

    const auto def = []( const char* name ) { return Find( name )->Default; };

    EXPECT_FLOAT_EQ( def( "Coverage" ).x, mirror.Coverage );
    EXPECT_FLOAT_EQ( def( "CoverageContrast" ).x, mirror.CoverageContrast );
    EXPECT_FLOAT_EQ( def( "WeatherTileSize" ).x, mirror.WeatherTileSize );
    EXPECT_EQ( static_cast<int32_t>( def( "Seed" ).x ), mirror.Seed );
    EXPECT_FLOAT_EQ( def( "PlacementDensity" ).x, mirror.PlacementDensity );
    EXPECT_FLOAT_EQ( def( "PlacementScatter" ).x, mirror.PlacementScatter );
    EXPECT_FLOAT_EQ( def( "PlacementSizeVariety" ).x, mirror.PlacementSizeVariety );
    EXPECT_FLOAT_EQ( def( "PatchTileSize" ).x, mirror.PatchTileSize );
    EXPECT_FLOAT_EQ( def( "PatchStrength" ).x, mirror.PatchStrength );
    EXPECT_FLOAT_EQ( def( "LayoutPatternStrength" ).x, mirror.LayoutPatternStrength );
    EXPECT_FLOAT_EQ( def( "LayoutMaskStrength" ).x, mirror.LayoutMaskStrength );
    EXPECT_EQ( static_cast<int32_t>( def( "LayoutRepeats" ).x ), mirror.LayoutRepeats );
    EXPECT_EQ( static_cast<int32_t>( def( "LayoutRotation" ).x ), mirror.LayoutRotation );
    EXPECT_FLOAT_EQ( def( "LayoutOffset" ).x, mirror.LayoutOffset.x );
    EXPECT_FLOAT_EQ( def( "LayoutOffset" ).y, mirror.LayoutOffset.y );
    EXPECT_FLOAT_EQ( def( "DetailTileSize" ).x, mirror.DetailTileSize );
    EXPECT_FLOAT_EQ( def( "DetailStrength" ).x, mirror.DetailStrength );
    EXPECT_FLOAT_EQ( def( "DensityScale" ).x, mirror.DensityScale );
    EXPECT_FLOAT_EQ( def( "ExtinctionScale" ).x, mirror.ExtinctionScale );
    EXPECT_FLOAT_EQ( def( "ScatteringAlbedo" ).x, mirror.ScatteringAlbedo.x );
    EXPECT_FLOAT_EQ( def( "ScatteringAlbedo" ).y, mirror.ScatteringAlbedo.y );
    EXPECT_FLOAT_EQ( def( "ScatteringAlbedo" ).z, mirror.ScatteringAlbedo.z );
    EXPECT_FLOAT_EQ( def( "PhaseG" ).x, mirror.PhaseG );
    EXPECT_FLOAT_EQ( def( "PhaseGBackward" ).x, mirror.PhaseGBackward );
    EXPECT_FLOAT_EQ( def( "PhaseBlend" ).x, mirror.PhaseBlend );
    EXPECT_FLOAT_EQ( def( "AmbientOcclusionStrength" ).x, mirror.AmbientOcclusionStrength );
    EXPECT_EQ( static_cast<int32_t>( def( "MultiScatterOctaves" ).x ), mirror.MultiScatterOctaves );
    EXPECT_FLOAT_EQ( def( "MultiScatterContribution" ).x, mirror.MultiScatterContribution );
    EXPECT_FLOAT_EQ( def( "MultiScatterOcclusion" ).x, mirror.MultiScatterOcclusion );
    EXPECT_FLOAT_EQ( def( "MultiScatterEccentricity" ).x, mirror.MultiScatterEccentricity );
    EXPECT_FLOAT_EQ( def( "AmbientScale" ).x, mirror.AmbientScale.x );
    EXPECT_FLOAT_EQ( def( "AmbientScale" ).y, mirror.AmbientScale.y );
    EXPECT_FLOAT_EQ( def( "AmbientScale" ).z, mirror.AmbientScale.z );
}

TEST( CloudMaterialSchema, EveryValueParameterHasItsSliderAndItsProse )
{
    for ( const auto& p : Schema().Params )
    {
        EXPECT_FALSE( p.Category.empty() ) << p.Name << " has no Category, so it lands in an unnamed group";
        EXPECT_FALSE( p.Tooltip.empty() ) << p.Name << " has no Tooltip — the component fields these "
                                          << "replaced all documented themselves, and the material must not "
                                          << "author blinder than the Details panel did";
        if ( p.IsAssetRef() )
            continue;
        using VT = Desert::Core::Formats::ShaderValueType;
        if ( p.Type == VT::Float || p.Type == VT::Int )
            EXPECT_TRUE( p.Min.has_value() && p.Max.has_value() )
                 << p.Name << " has no Range, so it draws as a bare drag field";
    }

    // The three copies of the octave ceiling stayed one number through the move: the schema's Range, the
    // packer's clamp and the shader's own loop bound all answer to kCloudMultiScatterMaxOctaves.
    EXPECT_FLOAT_EQ( Find( "MultiScatterOctaves" )->Max.value_or( 0.0f ),
                     static_cast<float>( Desert::ECS::kCloudMultiScatterMaxOctaves ) );

    // And every default sits inside its own slider — the relation ComponentReflection asserts for the
    // component's fields, continued for the schema's.
    for ( const auto& p : Schema().Params )
    {
        if ( !p.Min.has_value() || !p.Max.has_value() )
            continue;
        EXPECT_GE( p.Default.x, *p.Min ) << p.Name << " defaults below its own slider";
        EXPECT_LE( p.Default.x, *p.Max ) << p.Name << " defaults above its own slider";
    }
}

TEST( CloudMaterialSchema, BuildAppliesSchemaThenOverridesAndSkipsWhatItDoesNotKnow )
{
    // Defaults path: schema in, mirror out — the two agreeing is the previous test; this one asserts the
    // BUILDER takes them from the schema (a schema default perturbed in-memory must come through).
    ShaderProgramMeta perturbed = Schema();
    for ( auto& p : perturbed.Params )
        if ( p.Name == "Coverage" )
            p.Default.x = 0.77f;

    const CloudMaterialValues fromSchema = BuildCloudMaterialValues( &perturbed, MaterialOverrides{} );
    EXPECT_FLOAT_EQ( fromSchema.Coverage, 0.77f )
         << "the builder ignored the schema's default — the shader file is not the source of truth";

    // Overrides land last and win; asset references ride the name->handle map; an unknown name is
    // SKIPPED rather than guessed at (a .demat may be ahead of or behind this binary).
    MaterialOverrides overrides;
    overrides.Params.emplace_back( "Coverage", glm::vec4( 0.25f, 0.0f, 0.0f, 0.0f ) );
    overrides.Params.emplace_back( "AmbientScale", glm::vec4( 0.5f, 0.25f, 0.125f, 0.0f ) );
    overrides.Params.emplace_back( "MultiScatterOctaves", glm::vec4( 2.0f, 0.0f, 0.0f, 0.0f ) );
    overrides.Params.emplace_back( "NotAKnownParameter", glm::vec4( 42.0f ) );
    overrides.Textures.emplace_back( "CloudType3", 0xBEEFull );
    overrides.Textures.emplace_back( "LayoutPattern", 0xCAFEull );
    overrides.Textures.emplace_back( "LayoutMask", 0xFEEDull );
    overrides.Textures.emplace_back( "NotAKnownSlot", 0xDEADull );

    const CloudMaterialValues values = BuildCloudMaterialValues( &Schema(), overrides );
    EXPECT_FLOAT_EQ( values.Coverage, 0.25f );
    EXPECT_EQ( values.AmbientScale, glm::vec3( 0.5f, 0.25f, 0.125f ) );
    EXPECT_EQ( values.MultiScatterOctaves, 2 );
    EXPECT_EQ( static_cast<uint64_t>( values.CloudType3 ), 0xBEEFull );
    EXPECT_EQ( static_cast<uint64_t>( values.LayoutPattern ), 0xCAFEull );
    EXPECT_EQ( static_cast<uint64_t>( values.LayoutMask ), 0xFEEDull )
         << "the pattern and the mask are separate inputs; a reader that folded them together would "
            "pass this line with one of the two handles in both fields";
    // Untouched neighbours keep the schema defaults.
    EXPECT_FLOAT_EQ( values.CoverageContrast, CloudMaterialValues{}.CoverageContrast );
    EXPECT_EQ( static_cast<uint64_t>( values.CloudType1 ), 0ull );

    // No schema at all is the loud-degradation path: the mirror stands in, which the digit-equality test
    // above proves is the same sky.
    const CloudMaterialValues noSchema = BuildCloudMaterialValues( nullptr, overrides );
    EXPECT_FLOAT_EQ( noSchema.Coverage, 0.25f );
}

// THE ALBEDO IS READ AS THREE COMPONENTS AND NOT REPAIRED ON THE WAY IN — which is what makes the
// migration necessary rather than optional, and it is asserted here so nobody makes the reader "helpful".
//
// The tempting leniency is "if y and z are zero, broadcast x": it would make every unmigrated `.demat`
// render correctly. It is refused twice over. It makes (0.98, 0, 0) — a legal authored colour now that the
// slot has three components — inexpressible; and it hides an unraised file for ever, so the corpus would
// carry two shapes of the same value indefinitely and the next person to touch either end would meet both.
// Migration::MigrateCloudMaterialAlbedoToColour raises the file ONCE instead, and the SceneCloudMaterial-
// Migration suite is where that is tested.
TEST( CloudMaterialSchema, TheAlbedoIsAColourAndAnOldScalarIsNotQuietlyRepaired )
{
    MaterialOverrides scalarAsWritten;
    scalarAsWritten.Params.emplace_back( "ScatteringAlbedo", glm::vec4( 0.98f, 0.0f, 0.0f, 0.0f ) );

    const CloudMaterialValues stale = BuildCloudMaterialValues( &Schema(), scalarAsWritten );
    EXPECT_EQ( stale.ScatteringAlbedo, glm::vec3( 0.98f, 0.0f, 0.0f ) )
         << "the reader broadcast a scalar albedo into a colour. That makes an authored (0.98, 0, 0) "
            "impossible to express and lets an unmigrated .demat live for ever; the migrator raises the "
            "file instead.";

    MaterialOverrides authored;
    authored.Params.emplace_back( "ScatteringAlbedo", glm::vec4( 0.9f, 0.72f, 0.55f, 0.0f ) );
    EXPECT_EQ( BuildCloudMaterialValues( &Schema(), authored ).ScatteringAlbedo, glm::vec3( 0.9f, 0.72f, 0.55f ) );
}

// THE PROTOCOL SCENES' MATERIALS ARE FULLY EXPLICIT, which is the §PR instrument-property continued
// across the seam: those scenes exist so no default change can move a measurement, and after O1 a schema
// default could move one through a material that omitted a parameter. The migration writes every stated
// field; this is what keeps somebody from later "cleaning up" the redundant-looking values.
TEST( CloudMaterialSchema, TheProtocolScenesMaterialsStateEveryValueParameter )
{
    const char* const kProtocolMaterials[] = {
         "Materials/M_Clouds_Protocol_Clouds.demat",
         "Materials/M_PR_Hero0_Clouds.demat",
         "Materials/M_PR_Hero3_Clouds.demat",
         "Materials/M_PR_Hero8_Clouds.demat",
    };

    for ( const char* rel : kProtocolMaterials )
    {
        const std::string path = RepoRoot() + "Editor/Resources/Assets/" + rel;
        const std::string json = ReadAll( path );
        ASSERT_FALSE( json.empty() ) << path << " is missing — the protocol scene's look is exposed to "
                                     << "schema defaults again";

        // Membership by name is enough here (the values are the scene author's, not this suite's);
        // parsing the JSON with the engine's reflectors would drag half the engine into a parser suite.
        for ( const ValueRow& row : kValues )
            EXPECT_NE( json.find( std::string( "\"" ) + row.Name + "\"" ), std::string::npos )
                 << rel << " does not state " << row.Name << " — a schema default can now move the protocol's sky";
    }
}

// THE SHARED DEFAULT MATERIAL (D-37, teamlead 2026-09-06) MUST RESOLVE TO THE SAME SKY AS AN EMPTY
// SLOT USED TO — that is the whole point of routing every look-less scene at it instead of leaving
// Material null. The relation is proved by composing three already-separately-tested facts rather than
// re-parsing the file with the full engine (this suite deliberately links nothing past the shader
// parser and Common — see the premake5.lua comment):
//
//   1. TheSchemaDefaultsAreTheMirrorsToTheDigit: CloudMaterialValues{} == the schema's own defaults,
//      digit for digit.
//   2. BuildAppliesSchemaThenOverridesAndSkipsWhatItDoesNotKnow: BuildCloudMaterialValues(schema, {})
//      with an EMPTY MaterialOverrides returns exactly the schema's defaults — overrides only ever
//      move a value away from the default, never toward a second one.
//   3. THIS TEST: M_CloudDefault.demat states no Params and no Textures, which is exactly what an
//      empty MaterialOverrides looks like once loaded — so its resolution is (1) composed with (2),
//      with no numbers copied into the file for a future schema edit to fall out of step with.
//
// A file that stated even one baked-in value here would reintroduce the second-source-of-truth defect
// D-37 exists to remove: the DAY the schema's own default changes, a baked copy stops matching it
// silently, while an empty-overrides file tracks the schema by construction and cannot.
TEST( CloudMaterialSchema, TheSharedDefaultMaterialStatesNoOverridesAndSoCannotDriftFromTheSchema )
{
    const std::string path = RepoRoot() + "Editor/Resources/Assets/Materials/M_CloudDefault.demat";
    const std::string json = ReadAll( path );
    ASSERT_FALSE( json.empty() ) << path << " is missing — every scene the migration points at it "
                                 << "(D-37) fails to resolve a material at load";

    // Membership by text, on the same terms TheProtocolScenesMaterialsStateEveryValueParameter uses for
    // the opposite claim (states every value): this file must state NONE.
    EXPECT_NE( json.find( "\"ShaderName\":\"CloudRaymarch\"" ), std::string::npos )
         << path << " does not name the Volume-domain cloud shader";
    EXPECT_NE( json.find( "\"Params\":[]" ), std::string::npos )
         << path << " states a Param — it must defer to the schema's own default instead of copying it";
    EXPECT_NE( json.find( "\"Textures\":[]" ), std::string::npos )
         << path << " states a Texture — it must defer to the schema's own default instead of copying it";
}

// ---------------------------------------------------------------------------------------------------
// WHICH PARAMETERS COST SECONDS — MOVED, AND WHY THE MOVE IS THE POINT (O8-3, O1)
// ---------------------------------------------------------------------------------------------------
//
// THE COMPLAINT THIS ANSWERS, in the owner's words, twice: "I'd like the clouds in the preview to update
// straight away". Half of them already do. A cloud material's parameters fall into two classes with
// completely different costs — read by the CPU BAKE, or read by the MARCH — and until O1's timing pass
// nothing on screen distinguished them.
//
// THAT CLAIM USED TO BE PINNED HERE, BY READING THE RENDERER AS TEXT: the body of
// VolumetricCloudRenderer::BuildProceduralParams was brace-matched out of the .cpp and searched for
// `m_Material.<Name>`. It was the weakest form of guard in this subsystem, and it had the failure mode a
// text guard always has — the bake's material half moved into a header
// (Graphic::ApplyCloudMaterialToBakeParams, so that a suite could CALL it), every `m_Material.` read left
// this file's field of view at once, and the test would have gone red naming twenty parameters with
// nothing wrong with any of them.
//
// It is now Desert/Tests/Engine/CloudMaterialTiming, where each parameter is PERTURBED and the renderer's
// own rebake decision (Assets::CloudProceduralParamsEqual) is asked whether the volume has to be built
// again — the relation executed instead of grepped, and checked against the `Timing` attribute the shader
// now declares and the Material Editor now prints.
//
// WHAT STAYS HERE is the half that belongs to the SCHEMA rather than to the bake: every parameter carries
// the attribute at all. Without it the panel's heading has nothing to say, and a schema census is exactly
// where "a value nobody classified" has to be caught — this suite is the one that walks the Properties
// block.
TEST( CloudMaterialSchema, EveryParameterDeclaresWhenItsEditBecomesVisible )
{
    using Timing = ::Desert::Core::Formats::ShaderParamTiming;

    uint32_t rebake    = 0;
    uint32_t immediate = 0;

    for ( const ShaderParam& p : Schema().Params )
    {
        EXPECT_NE( p.Timing, Timing::Unspecified )
             << p.Name
             << " declares no Timing, so the Material Editor cannot tell an artist whether moving "
                "it costs a frame or fourteen seconds. Add Timing(Immediate) or Timing(Rebake) to "
                "its Properties line; CloudMaterialTiming then checks the one you chose against "
                "the bake itself.";

        if ( p.Timing == Timing::Rebake )
            ++rebake;
        else if ( p.Timing == Timing::Immediate )
            ++immediate;
    }

    // QUOTED, so a schema that silently shrank is visible: the loop above is vacuously green over an empty
    // parameter list, which is how a census stops counting anything without going red.
    std::printf( "[CloudMaterialSchema] %u of %u parameters declare Rebake; %u declare Immediate\n", rebake,
                 static_cast<uint32_t>( Schema().Params.size() ), immediate );
    EXPECT_EQ( rebake, 20u );
    // FIFTEEN SINCE THE MEDIUM SLOT, which is the fourteen march parameters plus the authored medium
    // itself. It is Immediate and CloudMaterialTiming MEASURES that it is: a medium is GPU code compiled
    // into the march, so no amount of authoring it can move an input of a bake that has already run.
    EXPECT_EQ( immediate, 15u );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
