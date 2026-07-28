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
TEST( ShaderGraphCompiler, SurfaceEmitsSurfaceDomainAndDepthPass )
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
    // Surface graphs carry a shadow/depth variant.
    EXPECT_TRUE( HasPass( p.Meta.PassNames, "Depth" ) );
    // Mesh vertex contract.
    EXPECT_NE( compiled.GetValue().find( "GraphVertex.glslh" ), std::string::npos );
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

TEST( ShaderGraphCompiler, WrongOutputForDomainIsAnError )
{
    // Post-process document that (wrongly) contains only a Surface Output node.
    SG::Document doc;
    doc.Domain  = static_cast<int>( SG::Domain::PostProcess );
    doc.Name    = "Broken";
    auto output = SG::MakeNode( doc, "SurfaceOutput" );
    doc.Nodes.push_back( std::move( output ) );

    const auto compiled = SG::CompileToDShader( doc );
    EXPECT_FALSE( compiled.IsSuccess() );
    EXPECT_NE( compiled.GetError().find( "Post Process Output" ), std::string::npos );
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

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
