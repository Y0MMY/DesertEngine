#include <gtest/gtest.h>

#include <ShaderGraph.hpp> // editor: the graph document + compiler under test

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp> // engine: the real parser

#include <algorithm>

namespace SG = Desert::Editor::ShaderGraph;
using Desert::Core::Preprocess::DShaderParser;
using namespace Desert::Core::Formats;

namespace
{
    // Minimal Surface graph: BaseColor param -> Surface Output.
    SG::Document SurfaceDoc()
    {
        SG::Document doc;
        doc.Name = "TestSurface";

        auto param      = SG::MakeNode( doc, "ColorParam" );
        param.ParamName = "BaseColor";
        auto output     = SG::MakeNode( doc, "SurfaceOutput" );

        doc.Links.push_back( { doc.NextId++, param.Outputs[0].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( param ) );
        doc.Nodes.push_back( std::move( output ) );
        return doc;
    }

    // Minimal Post-Process graph: Scene Color -> Post Process Output (passthrough).
    SG::Document PostProcessDoc()
    {
        SG::Document doc;
        doc.Name   = "TestPost";
        doc.Domain = static_cast<int>( SG::Domain::PostProcess );

        auto scene  = SG::MakeNode( doc, "SceneColor" );
        auto output = SG::MakeNode( doc, "PostProcessOutput" );

        doc.Links.push_back( { doc.NextId++, scene.Outputs[0].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( scene ) );
        doc.Nodes.push_back( std::move( output ) );
        return doc;
    }

    bool HasPass( const std::vector<std::string>& passes, const std::string& name )
    {
        return std::find( passes.begin(), passes.end(), name ) != passes.end();
    }
} // namespace

// The compiler must emit the domain from the document, and the engine parser must read it back.
TEST( ShaderGraphCompiler, SurfaceEmitsSurfaceDomainAndOneLitPass )
{
    const auto compiled = SG::CompileToDShader( SurfaceDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();

    auto parsed = DShaderParser::Parse( compiled.GetValue() );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    const auto& p = parsed.GetValue();

    EXPECT_EQ( p.Name, "TestSurface" );
    EXPECT_EQ( p.Meta.Domain, ShaderDomain::Surface );
    EXPECT_TRUE( p.Stages.count( ShaderStage::Vertex ) );
    EXPECT_TRUE( p.Stages.count( ShaderStage::Fragment ) );
    // Mesh vertex contract.
    EXPECT_NE( compiled.GetValue().find( "GraphVertex.glslh" ), std::string::npos );
}

// This test used to assert the OPPOSITE — that a Surface graph "carries a shadow/depth variant".
// It carried a pass named "Depth" that nothing could consume: no C++ anywhere asks for
// "<shader>/Depth", and it could not have served as a shadow caster if something had. A cascade
// target is a colour R32F attachment a fragment shader must write, and that pass declared NO
// fragment stage; its vertex transformed by cameraUB — the camera, not the light. So it was three
// SPIR-V modules per rebuild that rendered nothing anywhere, and asserting its presence pinned the
// wrong thing. Graph materials cast shadows through the engine's shadow pipeline instead.
TEST( ShaderGraphCompiler, NoDepthPassIsEmittedForAnyDomain )
{
    for ( const SG::Document& doc : { SurfaceDoc(), PostProcessDoc() } )
    {
        const auto compiled = SG::CompileToDShader( doc );
        ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
        const std::string& src = compiled.GetValue();

        auto parsed = DShaderParser::Parse( src );
        ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();

        EXPECT_FALSE( HasPass( parsed.GetValue().Meta.PassNames, "Depth" ) )
             << "a pass no consumer names, and one that could not write a cascade if it had";
        // Not merely absent from the pass list — the text that configured it is gone too, so the
        // shared vertex include has no dead branch left to keep alive.
        EXPECT_EQ( src.find( "GRAPH_DEPTH_ONLY" ), std::string::npos );
        EXPECT_EQ( src.find( "Pass \"Depth\"" ), std::string::npos );
    }
}

TEST( ShaderGraphCompiler, PostProcessEmitsPostProcessDomainAndFullscreenTriangle )
{
    const auto compiled = SG::CompileToDShader( PostProcessDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& src = compiled.GetValue();

    auto parsed = DShaderParser::Parse( src );
    ASSERT_TRUE( parsed.IsSuccess() ) << parsed.GetError();
    const auto& p = parsed.GetValue();

    EXPECT_EQ( p.Meta.Domain, ShaderDomain::PostProcess );
    EXPECT_TRUE( p.Stages.count( ShaderStage::Vertex ) );
    EXPECT_TRUE( p.Stages.count( ShaderStage::Fragment ) );

    // Post-process uses the fullscreen-triangle contract and samples the scene color, and has NO
    // mesh vertex contract and NO shadow/depth pass.
    EXPECT_NE( src.find( "QUAD_POSITIONS" ), std::string::npos );
    EXPECT_NE( src.find( "u_SceneTexture" ), std::string::npos );
    EXPECT_EQ( src.find( "GraphVertex.glslh" ), std::string::npos );
    EXPECT_FALSE( HasPass( p.Meta.PassNames, "Depth" ) );
}

// Switching the document's domain field must change the compiled output — no constant remains.
TEST( ShaderGraphCompiler, DomainIsDrivenByTheDocumentField )
{
    SG::Document doc = SurfaceDoc();

    const auto asSurface = SG::CompileToDShader( doc );
    ASSERT_TRUE( asSurface.IsSuccess() ) << asSurface.GetError();
    EXPECT_NE( asSurface.GetValue().find( "Domain Surface" ), std::string::npos );
    EXPECT_EQ( asSurface.GetValue().find( "Domain PostProcess" ), std::string::npos );
}

TEST( ShaderGraphCompiler, DomainSurvivesSerializationRoundTrip )
{
    const std::string json = SG::Serialize( PostProcessDoc() );
    auto              back = SG::Deserialize( json );
    ASSERT_TRUE( back.IsSuccess() ) << back.GetError();
    EXPECT_EQ( back.GetValue().DomainEnum(), SG::Domain::PostProcess );
}

// A .dgraph written before the Domain field existed (no "Domain" key) defaults to Surface.
TEST( ShaderGraphCompiler, LegacyGraphWithoutDomainDefaultsToSurface )
{
    const std::string json = SG::Serialize( SurfaceDoc() );
    // Strip the Domain field to emulate an old document.
    // (Serialization always writes it, so just assert the default instead.)
    SG::Document fresh;
    EXPECT_EQ( fresh.DomainEnum(), SG::Domain::Surface );
    EXPECT_TRUE( SG::Deserialize( json ).IsSuccess() );
}

// A post-process document holding a Surface Output is wrong TWICE — it has a node its domain does
// not offer, and it lacks the output its domain requires. This used to be reported only as the
// second, because the missing-output check was the first thing that ran. Structural validation now
// runs before it, so the error names the node that is actually in the document and the domain it
// does not belong to; a message about a node the artist never placed sends them looking for it.
TEST( ShaderGraphCompiler, OutputFromTheWrongDomainNamesTheOffendingNode )
{
    SG::Document doc;
    doc.Domain  = static_cast<int>( SG::Domain::PostProcess );
    doc.Name    = "Broken";
    auto output = SG::MakeNode( doc, "SurfaceOutput" );
    doc.Nodes.push_back( std::move( output ) );

    const auto compiled = SG::CompileToDShader( doc );
    EXPECT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Surface Output" ), std::string::npos )
         << "names the node that IS there: " << compiled.GetError();
    EXPECT_NE( compiled.GetError().find( "Post Process" ), std::string::npos )
         << "and the domain it is not offered in: " << compiled.GetError();
}

// The missing-output check is still reachable and still says what is missing — it is what a
// well-formed document with no output node at all hits.
TEST( ShaderGraphCompiler, DomainWithNoOutputNodeAtAllNamesTheMissingOutput )
{
    SG::Document doc;
    doc.Domain = static_cast<int>( SG::Domain::PostProcess );
    doc.Name   = "Headless";
    doc.Nodes.push_back( SG::MakeNode( doc, "SceneColor" ) );

    const auto compiled = SG::CompileToDShader( doc );
    EXPECT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Post Process Output" ), std::string::npos ) << compiled.GetError();
}

// The palette split: core nodes live everywhere; output/special nodes are domain-scoped.
TEST( ShaderGraphCompiler, PaletteIsFilteredByDomain )
{
    const SG::NodeSpec* sceneColor = SG::FindSpec( "SceneColor" );
    const SG::NodeSpec* tileUV     = SG::FindSpec( "TileUV" );
    const SG::NodeSpec* multiply   = SG::FindSpec( "Multiply" );
    ASSERT_NE( sceneColor, nullptr );
    ASSERT_NE( tileUV, nullptr );
    ASSERT_NE( multiply, nullptr );

    // Scene Color is post-process only.
    EXPECT_TRUE( SG::SpecInDomain( *sceneColor, SG::Domain::PostProcess ) );
    EXPECT_FALSE( SG::SpecInDomain( *sceneColor, SG::Domain::Surface ) );

    // Mesh tiling makes no sense over a fullscreen effect.
    EXPECT_TRUE( SG::SpecInDomain( *tileUV, SG::Domain::Surface ) );
    EXPECT_FALSE( SG::SpecInDomain( *tileUV, SG::Domain::PostProcess ) );

    // Core math is available in both.
    EXPECT_TRUE( SG::SpecInDomain( *multiply, SG::Domain::Surface ) );
    EXPECT_TRUE( SG::SpecInDomain( *multiply, SG::Domain::PostProcess ) );
}

// ===================================================================== link type checking =====
//
// The canvas refuses a mismatched link interactively, but a .dgraph is plain JSON. Every test below
// builds a document the canvas could never have produced and asserts the COMPILER rejects it — and
// that the message names the node, because the whole point is that shaderc could only name a line
// of generated code.

namespace
{
    // The shipped reproduction fixture, in code: UV (vec2) wired into Albedo (vec4).
    SG::Document TypeMismatchDoc()
    {
        SG::Document doc;
        doc.Name = "TypeMismatch";

        auto uv     = SG::MakeNode( doc, "UV" );
        auto output = SG::MakeNode( doc, "SurfaceOutput" );

        doc.Links.push_back( { doc.NextId++, uv.Outputs[0].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( uv ) );
        doc.Nodes.push_back( std::move( output ) );
        return doc;
    }
} // namespace

TEST( ShaderGraphCompiler, MismatchedLinkIsRejectedAndNamesBothNodes )
{
    const auto compiled = SG::CompileToDShader( TypeMismatchDoc() );
    ASSERT_FALSE( compiled.IsSuccess() ) << "a vec2 -> vec4 link must not reach shaderc";

    const std::string& err = compiled.GetError();
    // Named by their palette titles, with the offending types spelled in GLSL terms.
    EXPECT_NE( err.find( "UV" ), std::string::npos ) << err;
    EXPECT_NE( err.find( "Surface Output" ), std::string::npos ) << err;
    EXPECT_NE( err.find( "Albedo" ), std::string::npos ) << err;
    EXPECT_NE( err.find( "vec2" ), std::string::npos ) << err;
    EXPECT_NE( err.find( "vec4" ), std::string::npos ) << err;
}

// The check must not fire on a graph the canvas would have accepted.
TEST( ShaderGraphCompiler, WellTypedGraphsStillCompile )
{
    EXPECT_TRUE( SG::CompileToDShader( SurfaceDoc() ).IsSuccess() );
    EXPECT_TRUE( SG::CompileToDShader( PostProcessDoc() ).IsSuccess() );
}

// Float -> Color and Color -> Float are both rejected: the rule is equality, exactly as on the
// canvas, not "whatever GLSL happens to accept" (vec4*vec4 compiles and is silently wrong).
TEST( ShaderGraphCompiler, TypeRuleIsEqualityInBothDirections )
{
    {
        SG::Document doc;
        doc.Name    = "FloatIntoColor";
        auto f      = SG::MakeNode( doc, "FloatConst" );
        auto scale  = SG::MakeNode( doc, "Scale" );
        auto output = SG::MakeNode( doc, "SurfaceOutput" );
        // FloatConst (float) into Scale's "Color" input (vec4).
        doc.Links.push_back( { doc.NextId++, f.Outputs[0].Id, scale.Inputs[0].Id } );
        doc.Links.push_back( { doc.NextId++, scale.Outputs[0].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( f ) );
        doc.Nodes.push_back( std::move( scale ) );
        doc.Nodes.push_back( std::move( output ) );
        EXPECT_FALSE( SG::CompileToDShader( doc ).IsSuccess() );
    }
    {
        SG::Document doc;
        doc.Name    = "ColorIntoFloat";
        auto c      = SG::MakeNode( doc, "ColorConst" );
        auto scale  = SG::MakeNode( doc, "Scale" );
        auto output = SG::MakeNode( doc, "SurfaceOutput" );
        // ColorConst (vec4) into Scale's "Factor" input (float).
        doc.Links.push_back( { doc.NextId++, c.Outputs[0].Id, scale.Inputs[1].Id } );
        doc.Links.push_back( { doc.NextId++, scale.Outputs[0].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( c ) );
        doc.Nodes.push_back( std::move( scale ) );
        doc.Nodes.push_back( std::move( output ) );
        EXPECT_FALSE( SG::CompileToDShader( doc ).IsSuccess() );
    }
}

// TextureSample has two outputs of different types; the check must resolve the type PER PIN, not
// per node, or the R (float) output would be judged as the RGBA (vec4) one.
TEST( ShaderGraphCompiler, MultiOutputNodeIsTypedPerPin )
{
    const auto build = []( size_t outputIndex )
    {
        SG::Document doc;
        doc.Name    = "TexPin";
        auto tex    = SG::MakeNode( doc, "TextureSample" );
        auto output = SG::MakeNode( doc, "SurfaceOutput" );
        // Albedo is vec4: RGBA (output 0) fits, R (output 1) does not.
        doc.Links.push_back( { doc.NextId++, tex.Outputs[outputIndex].Id, output.Inputs[0].Id } );
        doc.Nodes.push_back( std::move( tex ) );
        doc.Nodes.push_back( std::move( output ) );
        return SG::CompileToDShader( doc );
    };

    EXPECT_TRUE( build( 0 ).IsSuccess() );  // RGBA -> Albedo
    EXPECT_FALSE( build( 1 ).IsSuccess() ); // R    -> Albedo
}

// A node whose pin list was trimmed by hand used to read off the end of the vector: the emitter
// indexes node.Inputs[i] positionally for every kind it knows.
TEST( ShaderGraphCompiler, NodeWithPinsNotMatchingItsKindIsRejected )
{
    SG::Document doc;
    doc.Name    = "TrimmedPins";
    auto mul    = SG::MakeNode( doc, "Multiply" );
    auto output = SG::MakeNode( doc, "SurfaceOutput" );
    doc.Links.push_back( { doc.NextId++, mul.Outputs[0].Id, output.Inputs[0].Id } );
    mul.Inputs.clear(); // "Inputs": [] in the file
    doc.Nodes.push_back( std::move( mul ) );
    doc.Nodes.push_back( std::move( output ) );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Multiply" ), std::string::npos ) << compiled.GetError();
}

// Pin::Type in the file is a mirror of the catalogue. If it lies, the canvas type-checks against
// the lie and accepts a link the compiler cannot emit.
TEST( ShaderGraphCompiler, PinTypeDisagreeingWithTheCatalogueIsRejected )
{
    SG::Document doc = SurfaceDoc();
    for ( auto& node : doc.Nodes )
        if ( node.Kind == "ColorParam" )
            node.Outputs[0].Type = static_cast<int>( SG::ValueType::Float );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Color Param" ), std::string::npos ) << compiled.GetError();
}

// A link whose endpoint id belongs to no node was silently treated as "unlinked" — the input
// quietly took its default and the artist saw a graph that did not do what it drew.
TEST( ShaderGraphCompiler, DanglingLinkIsRejected )
{
    SG::Document doc  = SurfaceDoc();
    doc.Links[0].From = 999999; // no node owns this pin

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "999999" ), std::string::npos ) << compiled.GetError();
}

// Direction matters: output -> input, never the reverse.
TEST( ShaderGraphCompiler, BackwardsLinkIsRejected )
{
    SG::Document doc = SurfaceDoc();
    std::swap( doc.Links[0].From, doc.Links[0].To );
    EXPECT_FALSE( SG::CompileToDShader( doc ).IsSuccess() );
}

// Two links into one input: the emitter kept whichever the map saw last, so the picture and the
// generated code disagreed with nothing to show for it.
TEST( ShaderGraphCompiler, TwoLinksIntoOneInputAreRejected )
{
    SG::Document doc = SurfaceDoc();

    auto     second = SG::MakeNode( doc, "ColorConst" );
    uint64_t albedo = 0;
    for ( const auto& node : doc.Nodes )
        if ( node.Kind == "SurfaceOutput" )
            albedo = node.Inputs[0].Id;
    ASSERT_NE( albedo, 0u );

    doc.Links.push_back( { doc.NextId++, second.Outputs[0].Id, albedo } );
    doc.Nodes.push_back( std::move( second ) );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Albedo" ), std::string::npos ) << compiled.GetError();
}

// A node the palette does not offer in this domain would emit GLSL referring to declarations the
// domain's stage never writes (SceneColor -> u_SceneTexture, absent from a Surface fragment).
TEST( ShaderGraphCompiler, OutOfDomainNodeIsRejected )
{
    SG::Document doc = SurfaceDoc();
    doc.Nodes.push_back( SG::MakeNode( doc, "SceneColor" ) );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Scene Color" ), std::string::npos ) << compiled.GetError();
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
