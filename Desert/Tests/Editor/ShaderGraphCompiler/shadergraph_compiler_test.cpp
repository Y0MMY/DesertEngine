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
    EXPECT_EQ( back.GetValue().Doc.DomainEnum(), SG::Domain::PostProcess );
    // A document this build wrote needs nothing done to it.
    EXPECT_EQ( back.GetValue().MigratedPins, 0 );
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

// ======================================================================= the lit surface ======
//
// A Surface graph with Lit ticked used to carry a lighting model written by THIS COMPILER: a flat
// `vec3( 0.12 )` ambient that read no environment, `albedo * cos` with no division by PI, and no
// cloud shadow. All three were defects the engine had already fixed by making the term one shared
// text (Р16, Р20, Р21) — the graph was a fourth copy nobody had migrated.

namespace
{
    SG::Document LitSurfaceDoc()
    {
        SG::Document doc = SurfaceDoc();
        doc.Name         = "TestLit";
        doc.Lit          = true;
        return doc;
    }
} // namespace

TEST( ShaderGraphCompiler, ALitSurfaceCallsTheSharedModelAndWritesNoFormulaOfItsOwn )
{
    const auto compiled = SG::CompileToDShader( LitSurfaceDoc() );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& src = compiled.GetValue();

    // The whole model behind one include and one call.
    EXPECT_NE( src.find( "Common/GraphSurfaceLighting.glslh" ), std::string::npos ) << src;
    EXPECT_NE( src.find( "ShadeGraphSurface(" ), std::string::npos ) << src;

    // And not one line of lighting arithmetic left in the generated text. These are the exact three
    // fragments the old emitter wrote, so this fails if any of them is reintroduced here rather than
    // fixed in the shared header where every other surface would get it too.
    EXPECT_EQ( src.find( "vec3( 0.12 )" ), std::string::npos ) << "the flat ambient constant is back";
    EXPECT_EQ( src.find( "max( dot( N, L ), 0.0 )" ), std::string::npos )
         << "the graph is computing its own Lambert term again";
    EXPECT_EQ( src.find( "ColorIntensity" ), std::string::npos )
         << "the graph is unpacking the light payload itself instead of calling the shared model";

    // The lit vertex contract carries what the model needs: world position and eye position, not only
    // a normal. A surface that knows only its normal cannot be given a cloud shadow or a point light.
    EXPECT_NE( src.find( "GRAPH_LIT" ), std::string::npos );
    EXPECT_NE( src.find( "v_WorldPos" ), std::string::npos );
    EXPECT_NE( src.find( "v_CameraPos" ), std::string::npos );

    // Unwired, the material attributes fall back to the standard material's schema defaults.
    EXPECT_NE( src.find( "albedo.rgb, 0.0, 0.5, 1.0" ), std::string::npos ) << src;
}

TEST( ShaderGraphCompiler, AnUnlitSurfaceIsUntouchedByTheShadingModel )
{
    const auto compiled = SG::CompileToDShader( SurfaceDoc() ); // Lit defaults to false
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    const std::string& src = compiled.GetValue();

    EXPECT_EQ( src.find( "GraphSurfaceLighting" ), std::string::npos ) << src;
    EXPECT_EQ( src.find( "ShadeGraphSurface" ), std::string::npos ) << src;
    EXPECT_EQ( src.find( "GRAPH_LIT" ), std::string::npos ) << src;
}

TEST( ShaderGraphCompiler, TheSurfaceOutputCarriesTheAttributesTheSharedModelConsumes )
{
    const SG::NodeSpec* spec = SG::FindSpec( "SurfaceOutput" );
    ASSERT_NE( spec, nullptr );

    // Both shared texts take metalness and roughness, and the ambient takes an occlusion factor. The
    // ORDER is part of the contract: pins are stored positionally in a .dgraph, so the three added by
    // Д16 are appended after Alpha and the first three keep their indices.
    ASSERT_EQ( spec->Inputs.size(), 6u );
    EXPECT_STREQ( spec->Inputs[0].Name, "Albedo" );
    EXPECT_STREQ( spec->Inputs[1].Name, "Emission" );
    EXPECT_STREQ( spec->Inputs[2].Name, "Alpha" );
    EXPECT_STREQ( spec->Inputs[3].Name, "Metallic" );
    EXPECT_STREQ( spec->Inputs[4].Name, "Roughness" );
    EXPECT_STREQ( spec->Inputs[5].Name, "Occlusion" );
}

TEST( ShaderGraphCompiler, TheGraphsOwnTexturesCannotLandOnAnEngineBinding )
{
    // The generated shader declares engine blocks at fixed slots (LightsMetadata 4, the point and spot
    // buffers 6 and 16, the IBL trio 8/9/10, DirectionLightsUB 14, TimeUB 15, the cloud pair 20/21 and,
    // since Д20, the five cascade bindings 5/7/13/22/23) and the parser numbers a Properties block's
    // textures upward from ONE base. While that base was 2, the third texture in a graph would have been
    // declared at binding 4 on top of LightsMetadata — two GLSL declarations on one descriptor, which
    // nothing reports. 23 is now the highest engine slot, so the base has to clear it and not 21.
    EXPECT_GT( SG::kGraphTextureBinding, 23u );

    SG::Document doc = LitSurfaceDoc();
    auto         tex = SG::MakeNode( doc, "TextureSample" );
    tex.ParamName    = "u_Mask";
    doc.Nodes.push_back( std::move( tex ) );

    const auto compiled = SG::CompileToDShader( doc );
    ASSERT_TRUE( compiled.IsSuccess() ) << compiled.GetError();
    EXPECT_NE( compiled.GetValue().find( "TextureBinding(24)" ), std::string::npos ) << compiled.GetValue();
}

// ========================================================================== the migration =====

TEST( ShaderGraphCompiler, ADocumentWrittenAgainstTheOlderCatalogueGrowsTheMissingPins )
{
    // A .dgraph exactly as builds before Д16 wrote it: a Surface Output with the three inputs the
    // catalogue had then. Verbatim rather than generated, because a document produced by THIS build can
    // never be short — the thing under test is a file on someone's disk.
    const std::string legacy =
         R"({"Name":"Legacy","NextId":8,"Domain":0,"Lit":true,"Nodes":[)"
         R"({"Id":1,"Kind":"ColorConst","ParamName":"","Value":[0.5,0.5,0.5,1.0],"X":0.0,"Y":0.0,)"
         R"("Inputs":[],"Outputs":[{"Id":2,"Name":"Color","Type":2}]},)"
         R"({"Id":3,"Kind":"SurfaceOutput","ParamName":"","Value":[1.0,1.0,1.0,1.0],"X":320.0,"Y":0.0,)"
         R"("Inputs":[{"Id":4,"Name":"Albedo","Type":2},{"Id":5,"Name":"Emission","Type":2},)"
         R"({"Id":6,"Name":"Alpha","Type":0}],"Outputs":[]}],)"
         R"("Links":[{"Id":7,"From":2,"To":4}]})";

    auto loaded = SG::Deserialize( legacy );
    ASSERT_TRUE( loaded.IsSuccess() ) << loaded.GetError();

    // Reported, never silent — the panel logs this number and puts it in the status line.
    EXPECT_EQ( loaded.GetValue().MigratedPins, 3 );

    const SG::Document& doc = loaded.GetValue().Doc;

    const SG::Node* output = nullptr;
    for ( const auto& node : doc.Nodes )
        if ( node.Kind == "SurfaceOutput" )
            output = &node;
    ASSERT_NE( output, nullptr );
    ASSERT_EQ( output->Inputs.size(), 6u );

    // THE PROPERTY that makes appending safe, and the reason the migration refuses anything else: the
    // pins that were already there keep their index AND their id, so the saved link still lands on
    // Albedo rather than on whatever now occupies index 0.
    EXPECT_EQ( output->Inputs[0].Id, 4u );
    EXPECT_EQ( output->Inputs[2].Id, 6u );
    ASSERT_EQ( doc.Links.size(), 1u );
    EXPECT_EQ( doc.Links[0].To, output->Inputs[0].Id );

    // New ids come out of the document's own allocator, so nothing collides with what is already there.
    EXPECT_GE( output->Inputs[3].Id, 8u );
    EXPECT_NE( output->Inputs[3].Id, output->Inputs[4].Id );
    EXPECT_GT( doc.NextId, output->Inputs[5].Id );

    // And it compiles — which is the whole point, because ValidateGraph rejects a node whose pin count
    // disagrees with its kind. Without the migration every .dgraph in existence stopped compiling.
    EXPECT_TRUE( SG::CompileToDShader( doc ).IsSuccess() );
}

TEST( ShaderGraphCompiler, MigrationRefusesToRewriteAGraphThatIsNotMerelyOld )
{
    // Appending is safe BECAUSE what is stored is a prefix of the catalogue. A node whose pins were
    // renamed, retyped or reordered is not an old document, it is a wrong one, and inventing the tail of
    // it would turn a diagnosable file into a silently different graph. It is left alone so
    // ValidateGraph names it.
    SG::Document doc    = LitSurfaceDoc();
    SG::Node*    output = nullptr;
    for ( auto& node : doc.Nodes )
        if ( node.Kind == "SurfaceOutput" )
            output = &node;
    ASSERT_NE( output, nullptr );

    output->Inputs.resize( 2 );
    output->Inputs[1].Name = "NotEmission";

    EXPECT_EQ( SG::MigrateToCatalogue( doc ), 0 );
    EXPECT_EQ( output->Inputs.size(), 2u );
    EXPECT_FALSE( SG::CompileToDShader( doc ).IsSuccess() );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
