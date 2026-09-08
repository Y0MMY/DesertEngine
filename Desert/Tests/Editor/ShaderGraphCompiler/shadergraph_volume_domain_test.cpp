// THE CLOUD MEDIUM DOMAIN: what a graph may author, what it may NOT, and the one relation that makes
// the whole mechanism shippable.
//
// ── WHY THE REFUSALS ARE THE INTERESTING HALF ────────────────────────────────────────────────────────
//
// A cloud material carries thirty-four values and about twenty of them are inputs to a CPU BAKE — a
// 256x32x256 volume built over several thousand cloud bodies, measured at 3.3 to 14.1 seconds on the
// development machine. The graph in this domain runs on the GPU, per sample, inside a march that reads
// the RESULT of that bake. So a bake input is not merely awkward to reach from a node: it is not in the
// shader's scope at all, and by the time the march runs it has already been consumed.
//
// The teamlead made this the acceptance condition of the architecture, in these terms: the split must be
// VISIBLE to the author in the UI **and** UNEXPRESSIBLE incorrectly — a red test, not a comment and not a
// convention. The visible half is Core::Formats::ShaderParamTiming, which every property of
// CloudRaymarch.shader declares and which the Material Editor draws. This file is the other half.
//
// ── AND IT IS DERIVED, NOT LISTED ────────────────────────────────────────────────────────────────────
//
// There is no hand-written list of parameter names anywhere below. The schema is PARSED out of the
// shipped CloudRaymarch.shader with the engine's own parser, and every assertion is a relation between
// that schema and the graph's own registers:
//
//   * every Rebake property must be REFUSED by the graph compiler, by name;
//   * every readable row must name a property that exists and is Immediate;
//   * every Immediate property must be in one register or the other, so a NEW property cannot be added
//     to the material without somebody deciding whether a graph may read it.
//
// A list would have gone stale on the first property anybody added. This goes red instead.

#include <gtest/gtest.h>

#include <ShaderGraph.hpp>

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace SG = Desert::Editor::ShaderGraph;
using Desert::Core::Preprocess::DShaderParser;
using namespace Desert::Core::Formats;

namespace
{
    std::filesystem::path RepoRoot()
    {
        std::filesystem::path prefix = ".";
        for ( int up = 0; up < 6; ++up )
        {
            if ( std::filesystem::exists( prefix / "Desert/Desert/Source/Engine/Core/SceneSettings.hpp" ) )
                return prefix;
            prefix /= "..";
        }
        return {};
    }

    std::string ReadAll( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    /// The SHIPPED cloud material schema, parsed with the engine's own parser. Everything below is a
    /// relation against this rather than against a copy of it.
    const std::vector<ShaderParam>& CloudSchema()
    {
        static const std::vector<ShaderParam> s_Params = []
        {
            const auto source =
                 ReadAll( RepoRoot() / "Editor/Resources/Shaders/Programs/Clouds/CloudRaymarch.shader" );
            const auto parsed = DShaderParser::Parse( source );
            EXPECT_TRUE( parsed.IsSuccess() )
                 << "the shipped cloud material shader did not parse, so nothing below means anything: "
                 << ( parsed.IsSuccess() ? std::string{} : parsed.GetError() );
            return parsed.IsSuccess() ? parsed.GetValue().Meta.Params : std::vector<ShaderParam>{};
        }();
        return s_Params;
    }

    /// A Volume graph made of exactly the two nodes a new one starts with, and NOTHING wired: the
    /// starter graph the Content Browser creates.
    SG::Document EmptyVolumeDoc()
    {
        SG::Document doc;
        doc.Name   = "TestMedium";
        doc.Domain = static_cast<int>( SG::Domain::Volume );

        auto sample = SG::MakeNode( doc, "CloudSample" );
        auto output = SG::MakeNode( doc, "VolumeOutput" );
        doc.Nodes.push_back( std::move( sample ) );
        doc.Nodes.push_back( std::move( output ) );
        return doc;
    }

    size_t IndexOfInput( const SG::Node& node, const char* name )
    {
        for ( size_t i = 0; i < node.Inputs.size(); ++i )
            if ( node.Inputs[i].Name == name )
                return i;
        return node.Inputs.size();
    }

    SG::Node& NodeOfKind( SG::Document& doc, const char* kind )
    {
        for ( auto& node : doc.Nodes )
            if ( node.Kind == kind )
                return node;
        ADD_FAILURE() << "the document has no '" << kind << "' node";
        return doc.Nodes.front();
    }
} // namespace

// The text a starter Volume graph compiles to, on stdout. Disabled because it asserts nothing; run it
// with
//   ./ShaderGraphCompiler --gtest_also_run_disabled_tests --gtest_filter=*DumpNeutralMedium*
// to obtain a medium that can be dropped on a real cloud material and shot — which is how the
// byte-for-byte half of this mechanism's acceptance is taken, and it needs the EMITTER's own output
// rather than a hand-written copy of it.
TEST( ShaderGraphVolumeDomain, DISABLED_DumpNeutralMedium )
{
    const auto compiled = SG::CompileToDShader( EmptyVolumeDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    std::printf( "%s", compiled.GetValue().c_str() );
}

// The same, for a graph that actually WIRES something: the shipped density scaled by a material
// parameter, a warm albedo built from the sample's own depth, and an emission. Its purpose is to be
// handed to shaderc — a text assertion cannot tell whether generated GLSL COMPILES, and the emitter is
// the only thing between an artist's canvas and a shader that does not.
//   ./ShaderGraphCompiler --gtest_also_run_disabled_tests --gtest_filter=*DumpWiredMedium*
TEST( ShaderGraphVolumeDomain, DISABLED_DumpWiredMedium )
{
    SG::Document doc = EmptyVolumeDoc();

    auto           defaults = SG::MakeNode( doc, "DefaultDensity" );
    const uint64_t defOut   = defaults.Outputs[0].Id;
    doc.Nodes.push_back( std::move( defaults ) );

    auto strength         = SG::MakeNode( doc, "CloudParam" );
    strength.ParamName    = "DetailStrength";
    const uint64_t strOut = strength.Outputs[0].Id;
    doc.Nodes.push_back( std::move( strength ) );

    auto           scaled  = SG::MakeNode( doc, "MultiplyFloat" );
    const uint64_t scaledA = scaled.Inputs[0].Id;
    const uint64_t scaledB = scaled.Inputs[1].Id;
    const uint64_t scaledO = scaled.Outputs[0].Id;
    doc.Nodes.push_back( std::move( scaled ) );

    auto warm            = SG::MakeNode( doc, "Vec3Const" );
    warm.Value           = { 1.0f, 0.85f, 0.7f, 0.0f };
    const uint64_t warmO = warm.Outputs[0].Id;
    doc.Nodes.push_back( std::move( warm ) );

    auto           layer  = SG::MakeNode( doc, "LayerAlbedo" );
    const uint64_t layerO = layer.Outputs[0].Id;
    doc.Nodes.push_back( std::move( layer ) );

    auto           tinted  = SG::MakeNode( doc, "MultiplyVec3" );
    const uint64_t tintedA = tinted.Inputs[0].Id;
    const uint64_t tintedB = tinted.Inputs[1].Id;
    const uint64_t tintedO = tinted.Outputs[0].Id;
    doc.Nodes.push_back( std::move( tinted ) );

    auto           sample  = SG::MakeNode( doc, "CloudSample" );
    const uint64_t profile = sample.Outputs[2].Id; // Profile
    doc.Nodes.push_back( std::move( sample ) );

    auto           glow  = SG::MakeNode( doc, "ScaleVec3" );
    const uint64_t glowA = glow.Inputs[0].Id;
    const uint64_t glowB = glow.Inputs[1].Id;
    const uint64_t glowO = glow.Outputs[0].Id;
    doc.Nodes.push_back( std::move( glow ) );

    auto emit            = SG::MakeNode( doc, "Vec3Const" );
    emit.Value           = { 0.2f, 0.05f, 0.0f, 0.0f };
    const uint64_t emitO = emit.Outputs[0].Id;
    doc.Nodes.push_back( std::move( emit ) );

    SG::Node& out = NodeOfKind( doc, "VolumeOutput" );
    doc.Links.push_back( { doc.NextId++, defOut, scaledA } );
    doc.Links.push_back( { doc.NextId++, strOut, scaledB } );
    doc.Links.push_back( { doc.NextId++, scaledO, out.Inputs[IndexOfInput( out, "Density" )].Id } );
    doc.Links.push_back( { doc.NextId++, layerO, tintedA } );
    doc.Links.push_back( { doc.NextId++, warmO, tintedB } );
    doc.Links.push_back( { doc.NextId++, tintedO, out.Inputs[IndexOfInput( out, "Albedo" )].Id } );
    doc.Links.push_back( { doc.NextId++, emitO, glowA } );
    doc.Links.push_back( { doc.NextId++, profile, glowB } );
    doc.Links.push_back( { doc.NextId++, glowO, out.Inputs[IndexOfInput( out, "Emissive" )].Id } );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    std::printf( "%s", compiled.GetValue().c_str() );
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════
// THE BAKE/MARCH SPLIT — the teamlead's acceptance condition
// ═════════════════════════════════════════════════════════════════════════════════════════════════════

TEST( ShaderGraphVolumeDomain, NoBakeInputIsReachableAsAnInputOfAnyGraphNode )
{
    // THE RED TEST THE CONDITION ASKS FOR. For every property the schema marks Timing(Rebake), a graph
    // that names it must be REFUSED — with the property's own name in the message, because the artist's
    // question is "why can I not use Coverage here" and the answer has to name Coverage.
    int rebakeCount = 0;
    for ( const auto& param : CloudSchema() )
    {
        if ( param.Timing != ShaderParamTiming::Rebake )
            continue;
        ++rebakeCount;

        // It is not in the readable register...
        const bool readable =
             std::any_of( SG::VolumeParams().begin(), SG::VolumeParams().end(),
                          [&param]( const SG::VolumeParam& row ) { return param.Name == row.SchemaName; } );
        EXPECT_FALSE( readable ) << "'" << param.Name
                                 << "' is an input to the CPU bake and the Volume domain offers it as a "
                                    "readable graph value. By the time the march samples anything that "
                                    "bake has already run: the node would read a value that cannot "
                                    "affect the frame it is read in.";

        // ...and a document that names it anyway does not compile.
        SG::Document doc   = EmptyVolumeDoc();
        auto         node  = SG::MakeNode( doc, "CloudParam" );
        node.ParamName     = param.Name;
        const uint64_t out = node.Outputs[0].Id;
        doc.Nodes.push_back( std::move( node ) );

        SG::Node&    output = NodeOfKind( doc, "VolumeOutput" );
        const size_t pin    = IndexOfInput( output, "Density" );
        ASSERT_LT( pin, output.Inputs.size() );
        doc.Links.push_back( { doc.NextId++, out, output.Inputs[pin].Id } );

        const auto compiled = SG::CompileToDShader( doc );
        ASSERT_FALSE( compiled.IsSuccess() )
             << "a graph reading the bake input '" << param.Name
             << "' compiled. The split between what the CPU bakes and what the GPU marches would then be "
                "a convention rather than a rule, which is exactly what this suite exists to prevent.";
        EXPECT_NE( compiled.GetError().find( param.Name ), std::string::npos )
             << "the refusal does not name '" << param.Name
             << "', so the artist is told that something is wrong without being told what: "
             << compiled.GetError();
    }

    EXPECT_GT( rebakeCount, 0 ) << "the schema declares no Rebake property at all, so this test asserted "
                                   "nothing. Either the parse failed or the Timing attribute was dropped "
                                   "from CloudRaymarch.shader.";
    std::printf( "[ShaderGraphVolumeDomain] %d bake inputs refused as graph values\n", rebakeCount );
}

TEST( ShaderGraphVolumeDomain, EveryReadableRowNamesAnImmediatePropertyThatExists )
{
    ASSERT_FALSE( CloudSchema().empty() );

    for ( const auto& row : SG::VolumeParams() )
    {
        const auto param = std::find_if( CloudSchema().begin(), CloudSchema().end(),
                                         [&row]( const ShaderParam& p ) { return p.Name == row.SchemaName; } );
        ASSERT_NE( param, CloudSchema().end() )
             << "the Volume domain offers cloud material property '" << row.SchemaName
             << "', which the shipped schema does not declare. The node would emit GLSL naming a field "
                "that does not exist, or — worse — one that means something else.";
        EXPECT_EQ( param->Timing, ShaderParamTiming::Immediate )
             << "'" << row.SchemaName << "' is offered to graphs but is declared "
             << ShaderParamTimingName( param->Timing )
             << ". Only a value the march reads per frame may be read per sample.";
        EXPECT_TRUE( param->AssetKind.empty() ) << "'" << row.SchemaName
                                                << "' is an ASSET REFERENCE, which is a CPU-side handle and not a "
                                                   "number the shader can read at all.";
        EXPECT_NE( std::string( row.Expression ), "" );
        EXPECT_NE( std::string( row.Units ), "" )
             << "'" << row.SchemaName
             << "' has no Units note. DetailTileSize is centimetres in the panel and KILOMETRES in the "
                "medium; a row without that sentence is how the two come to be confused.";
    }
}

TEST( ShaderGraphVolumeDomain, EveryImmediatePropertyIsInOneRegisterOrTheOther )
{
    // THE DIRECTION THAT CATCHES A NEW PROPERTY. Adding one to CloudRaymarch.shader and forgetting the
    // graph entirely is the silent case: the material grows a knob, the graph cannot see it, and nothing
    // anywhere says whether that was intended. Here it is a red test with the property's name in it.
    ASSERT_FALSE( CloudSchema().empty() );

    for ( const auto& param : CloudSchema() )
    {
        if ( param.Timing != ShaderParamTiming::Immediate || !param.AssetKind.empty() )
            continue;

        const bool readable =
             std::any_of( SG::VolumeParams().begin(), SG::VolumeParams().end(),
                          [&param]( const SG::VolumeParam& row ) { return param.Name == row.SchemaName; } );
        const auto excused = std::find_if(
             SG::VolumeParamsOutOfScope().begin(), SG::VolumeParamsOutOfScope().end(),
             [&param]( const SG::VolumeParamOutOfScope& row ) { return param.Name == row.SchemaName; } );
        const bool isExcused = excused != SG::VolumeParamsOutOfScope().end();

        EXPECT_NE( readable, isExcused )
             << "'" << param.Name << "' is "
             << ( readable ? "in BOTH registers, so one of them is lying about it"
                           : "in NEITHER register. It is a per-frame value of the cloud material, so "
                             "somebody has to say whether a medium graph may read it — add it to "
                             "ShaderGraph::VolumeParams(), or to VolumeParamsOutOfScope() with the reason." );

        if ( isExcused )
            EXPECT_FALSE( std::string( excused->Reason ).empty() )
                 << "'" << param.Name
                 << "' is excused with an empty reason, which is the same as no "
                    "register at all: the next reader cannot tell whether the "
                    "omission was decided or forgotten.";
    }
}

TEST( ShaderGraphVolumeDomain, NoAssetSlotOrBakeInputIsEvenNAMEABLEByANode )
{
    // The compiler's refusal above is about a graph that already names one. This is about the palette:
    // a Cloud Material Param node dropped fresh must name something LEGAL, or every new node would be an
    // error the artist has to fix before anything compiles.
    SG::Document doc  = EmptyVolumeDoc();
    const auto   node = SG::MakeNode( doc, "CloudParam" );

    const bool readable =
         std::any_of( SG::VolumeParams().begin(), SG::VolumeParams().end(),
                      [&node]( const SG::VolumeParam& row ) { return node.ParamName == row.SchemaName; } );
    EXPECT_TRUE( readable ) << "a freshly created Cloud Material Param node defaults to '" << node.ParamName
                            << "', which the domain does not expose.";
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════
// THE NEGATIVE CONTROL — a graph that changes nothing
// ═════════════════════════════════════════════════════════════════════════════════════════════════════

TEST( ShaderGraphVolumeDomain, AGraphWithNothingWiredCompilesToTheShippedMediumExactly )
{
    // THE ACCEPTANCE OF THE WHOLE MECHANISM, expressed as text. Every unconnected output pin emits the
    // engine's own default, so the starter graph is the shipped medium under another name — which is
    // what makes "assigning a new graph to a layer changes not one pixel" a thing that can be true.
    // The FRAME half of this claim is measured with DomeSweep; this half is what makes it inspectable.
    const auto compiled = SG::CompileToDShader( EmptyVolumeDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& text = compiled.GetValue();

    for ( const char* forward :
          { "return CloudDefaultDensity( params, field, positionKm );",
            "return CloudDefaultExtinctionFactor( params, field, positionKm );",
            "return CloudDefaultAlbedo( params, field, positionKm, materialAlbedo );",
            "return CloudDefaultEmissive( params, field, positionKm );",
            "return CloudDefaultOcclusion( params, field, positionKm, ambientOcclusion );" } )
    {
        EXPECT_NE( text.find( forward ), std::string::npos )
             << "an unwired Volume Output pin did not compile to the shipped default. Expected:\n  " << forward
             << "\ngot:\n"
             << text;
    }

    // And the Cloud Sample node, which IS in the document, contributes nothing to a function that does
    // not read it: five separate compilations, not one body with five returns.
    EXPECT_EQ( text.find( "CloudGraphSampleAt" ), std::string::npos )
         << "a node nothing reads was emitted anyway, so every medium would evaluate the whole graph "
            "five times per sample.";
}

TEST( ShaderGraphVolumeDomain, TheEmittedMediumIsAVolumeShaderWithNoStagesOfItsOwn )
{
    const auto compiled = SG::CompileToDShader( EmptyVolumeDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();

    const auto parsed = DShaderParser::Parse( compiled.GetValue() );
    ASSERT_TRUE( parsed.IsSuccess() ) << "the graph emitted text the engine's own parser rejects: "
                                      << parsed.GetError();

    EXPECT_EQ( parsed.GetValue().Meta.Domain, ShaderDomain::Volume );
    EXPECT_TRUE( parsed.GetValue().Meta.IsMediumProgram() )
         << "the emitted shader carries no Medium block, so nothing would ever substitute it.";
    EXPECT_TRUE( parsed.GetValue().Stages.empty() )
         << "a medium is compiled INTO other programs; a stage of its own would be a program nothing "
            "draws.";
    EXPECT_TRUE( parsed.GetValue().Meta.Params.empty() )
         << "the Volume domain exposes no properties of its own — its bindings would have to be free in "
            "all four programs that compile it, and a binding collision between two GLSL declarations is "
            "silent.";
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════
// A GRAPH THAT DOES CHANGE SOMETHING CHANGES IT WHERE IT SAID
// ═════════════════════════════════════════════════════════════════════════════════════════════════════

TEST( ShaderGraphVolumeDomain, AnAuthoredDensityReachesTheDensityFunctionAndNoOther )
{
    SG::Document doc = EmptyVolumeDoc();

    auto half              = SG::MakeNode( doc, "FloatConst" );
    half.Value             = { 0.5f, 0.0f, 0.0f, 0.0f };
    const uint64_t halfOut = half.Outputs[0].Id;
    doc.Nodes.push_back( std::move( half ) );

    auto           scale    = SG::MakeNode( doc, "MultiplyFloat" );
    auto           defaults = SG::MakeNode( doc, "DefaultDensity" );
    const uint64_t scaleA   = scale.Inputs[0].Id;
    const uint64_t scaleB   = scale.Inputs[1].Id;
    const uint64_t scaleOut = scale.Outputs[0].Id;
    const uint64_t defOut   = defaults.Outputs[0].Id;
    doc.Nodes.push_back( std::move( scale ) );
    doc.Nodes.push_back( std::move( defaults ) );

    SG::Node&    output = NodeOfKind( doc, "VolumeOutput" );
    const size_t pin    = IndexOfInput( output, "Density" );
    ASSERT_LT( pin, output.Inputs.size() );

    doc.Links.push_back( { doc.NextId++, defOut, scaleA } );
    doc.Links.push_back( { doc.NextId++, halfOut, scaleB } );
    doc.Links.push_back( { doc.NextId++, scaleOut, output.Inputs[pin].Id } );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& text = compiled.GetValue();

    // The density function carries the product...
    EXPECT_NE( text.find( "= CloudDefaultDensity( params, field, positionKm );" ), std::string::npos )
         << "the Default Density node did not emit a call to the shipped chain:\n"
         << text;
    EXPECT_NE( text.find( " * " ), std::string::npos );

    // ...and the four it was not wired into are untouched.
    for ( const char* untouched :
          { "return CloudDefaultExtinctionFactor( params, field, positionKm );",
            "return CloudDefaultAlbedo( params, field, positionKm, materialAlbedo );",
            "return CloudDefaultEmissive( params, field, positionKm );",
            "return CloudDefaultOcclusion( params, field, positionKm, ambientOcclusion );" } )
    {
        EXPECT_NE( text.find( untouched ), std::string::npos )
             << "authoring the DENSITY changed another output too. Expected to still find:\n  " << untouched;
    }
}

TEST( ShaderGraphVolumeDomain, EmissionIsAPinAndReachesTheEmissiveFunction )
{
    // The third output of the accepted contract, and the one that had no home before this domain: a
    // constant emission over a whole layer authors badly and would have cost two more unread slots in
    // the parameter block, so it enters the engine as a pin or not at all (O1_DESIGN §11.5).
    SG::Document doc = EmptyVolumeDoc();

    auto glow              = SG::MakeNode( doc, "Vec3Const" );
    glow.Value             = { 2.0f, 0.5f, 0.1f, 0.0f };
    const uint64_t glowOut = glow.Outputs[0].Id;
    doc.Nodes.push_back( std::move( glow ) );

    SG::Node&    output = NodeOfKind( doc, "VolumeOutput" );
    const size_t pin    = IndexOfInput( output, "Emissive" );
    ASSERT_LT( pin, output.Inputs.size() );
    doc.Links.push_back( { doc.NextId++, glowOut, output.Inputs[pin].Id } );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& text = compiled.GetValue();

    EXPECT_NE( text.find( "vec3( 2.0, 0.5, 0.1 )" ), std::string::npos ) << text;
    EXPECT_EQ( text.find( "return CloudDefaultEmissive( params, field, positionKm );" ), std::string::npos )
         << "the Emissive pin was wired and the function still returns the default.";
}

TEST( ShaderGraphVolumeDomain, TheLayersOwnValuesAreRefusedOutsideTheOutputTheyBelongTo )
{
    // Layer Albedo is the `materialAlbedo` ARGUMENT of one of the five functions. Reachable from the
    // Density output it would emit GLSL naming an undeclared variable — a compile error from generated
    // code, on a line the artist never wrote. Refused here instead, naming the node and the pin.
    SG::Document doc = EmptyVolumeDoc();

    auto           layer    = SG::MakeNode( doc, "LayerAlbedo" );
    auto           split    = SG::MakeNode( doc, "SplitVec3" );
    const uint64_t layerOut = layer.Outputs[0].Id;
    const uint64_t splitIn  = split.Inputs[0].Id;
    const uint64_t splitX   = split.Outputs[0].Id;
    doc.Nodes.push_back( std::move( layer ) );
    doc.Nodes.push_back( std::move( split ) );

    SG::Node&    output = NodeOfKind( doc, "VolumeOutput" );
    const size_t pin    = IndexOfInput( output, "Density" );
    ASSERT_LT( pin, output.Inputs.size() );
    doc.Links.push_back( { doc.NextId++, layerOut, splitIn } );
    doc.Links.push_back( { doc.NextId++, splitX, output.Inputs[pin].Id } );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() )
         << "the layer's own albedo was read from the density function, where it does not exist.";
    EXPECT_NE( compiled.GetError().find( "Albedo" ), std::string::npos ) << compiled.GetError();

    // ...and it IS legal where it belongs.
    SG::Document   ok      = EmptyVolumeDoc();
    auto           fine    = SG::MakeNode( ok, "LayerAlbedo" );
    const uint64_t fineOut = fine.Outputs[0].Id;
    ok.Nodes.push_back( std::move( fine ) );
    SG::Node&    okOutput = NodeOfKind( ok, "VolumeOutput" );
    const size_t okPin    = IndexOfInput( okOutput, "Albedo" );
    ASSERT_LT( okPin, okOutput.Inputs.size() );
    ok.Links.push_back( { ok.NextId++, fineOut, okOutput.Inputs[okPin].Id } );

    const auto okCompiled = SG::CompileToDShader( ok );
    EXPECT_TRUE( okCompiled.IsSuccess() ) << okCompiled.GetError();
}

// ═════════════════════════════════════════════════════════════════════════════════════════════════════
// THE PALETTE
// ═════════════════════════════════════════════════════════════════════════════════════════════════════

TEST( ShaderGraphVolumeDomain, TheDomainOffersNoNodeThatDeclaresABindingOrNeedsAVertexStage )
{
    // A medium is compiled into four programs whose descriptor sets are hand-built at bindings 0..24. A
    // node that declared a texture or an exposed property would have to pick a number free in all four,
    // and a collision between two GLSL declarations at one binding is SILENT — the engine writes the
    // descriptor twice and the shader reads whichever it got. Until that census exists, the domain
    // offers no such node, and this is what says so.
    for ( const auto& spec : SG::Specs() )
    {
        if ( !SG::SpecInDomain( spec, SG::Domain::Volume ) )
            continue;
        EXPECT_FALSE( spec.HasParamName && std::string( spec.Kind ) != "CloudParam" )
             << "node '" << spec.Kind
             << "' is offered in the Cloud Medium domain and exposes a property of its own, which would "
                "need a binding that is free in all four consumers.";
        EXPECT_NE( std::string( spec.Kind ), "TextureSample" );
        EXPECT_NE( std::string( spec.Kind ), "UV" );
        EXPECT_NE( std::string( spec.Kind ), "SceneColor" );
        EXPECT_NE( std::string( spec.Kind ), "Time" );
    }
}

TEST( ShaderGraphVolumeDomain, EveryVolumeNodeHasACompilerRule )
{
    // The catalogue drives both the palette and the compiler, and the compiler's `else` branch answers a
    // kind it does not know with an error rather than a silent black. Reaching every Volume node once is
    // what turns that branch from a hope into a check.
    for ( const auto& spec : SG::Specs() )
    {
        if ( !SG::SpecInDomain( spec, SG::Domain::Volume ) || std::string( spec.Kind ) == "VolumeOutput" )
            continue;
        if ( spec.Outputs.empty() )
            continue;

        // Each node is wired into the output its type fits, so that a missing rule is the only reason
        // this can fail.
        const char* pin = nullptr;
        switch ( spec.Outputs[0].Type )
        {
            case SG::ValueType::Float:
                pin = std::string( spec.Kind ) == "LayerOcclusion" ? "AmbientOcclusion" : "Density";
                break;
            case SG::ValueType::Vec3:
                pin = std::string( spec.Kind ) == "LayerAlbedo" ? "Albedo" : "Emissive";
                break;
            default:
                continue; // no Volume output pin of that type to wire it into
        }

        SG::Document   doc  = EmptyVolumeDoc();
        auto           node = SG::MakeNode( doc, spec.Kind );
        const uint64_t out  = node.Outputs[0].Id;
        doc.Nodes.push_back( std::move( node ) );

        SG::Node&    output = NodeOfKind( doc, "VolumeOutput" );
        const size_t index  = IndexOfInput( output, pin );
        ASSERT_LT( index, output.Inputs.size() ) << spec.Kind;
        doc.Links.push_back( { doc.NextId++, out, output.Inputs[index].Id } );

        const auto compiled = SG::CompileToDShader( doc );
        EXPECT_TRUE( compiled.IsSuccess() )
             << "node '" << spec.Kind
             << "' is in the Cloud Medium palette and does not compile: " << compiled.GetError();
    }
}
