#include <gtest/gtest.h>

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

using Desert::Core::Preprocess::DShaderParser;
using namespace Desert::Core::Formats;

namespace
{
    const char* kUnlit = R"(
// comment before the shader
Shader "Unlit"
{
    Domain Surface

    Properties Binding(1) TextureBinding(2)
    {
        Color     Color       ("Base Color")               = (0.8, 0.4, 0.1, 1)
        Float     Tiling      ("Tiling", Range(0.25, 64))  = 4
        Texture2D u_AlbedoTex ("Albedo")                   = "white"
    }

    State
    {
        Cull Back
        ZTest LEqual
        ZWrite On
        Blend Alpha
    }

    Vertex
    {
        void main() { gl_Position = vec4(0.0); }
    }

    Fragment
    {
        layout( location = 0 ) out vec4 o_Color;
        void main() { o_Color = texture( u_AlbedoTex, vec2(0.5) ) * u_Material.Color; }
    }
}
)";
}

TEST( DShaderParser, DetectsFormat )
{
    EXPECT_TRUE( DShaderParser::IsDShader( kUnlit ) );
    EXPECT_TRUE( DShaderParser::IsDShader( "  /* hi */ Shader \"X\" {}" ) );
    EXPECT_FALSE( DShaderParser::IsDShader( "#pragma program Grid\n#pragma use_stage vertex \"a.vert\"" ) );
    EXPECT_FALSE( DShaderParser::IsDShader( "" ) );
}

TEST( DShaderParser, ParsesNameDomainAndStages )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& p = res.GetValue();

    EXPECT_EQ( p.Name, "Unlit" );
    EXPECT_EQ( p.Meta.Domain, ShaderDomain::Surface );
    ASSERT_EQ( p.Stages.size(), 2u );
    ASSERT_TRUE( p.Stages.count( ShaderStage::Vertex ) );
    ASSERT_TRUE( p.Stages.count( ShaderStage::Fragment ) );
}

TEST( DShaderParser, ParsesProperties )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& params = res.GetValue().Meta.Params;

    ASSERT_EQ( params.size(), 3u );

    EXPECT_EQ( params[0].Name, "Color" );
    EXPECT_EQ( params[0].DisplayName, "Base Color" );
    EXPECT_EQ( params[0].Type, ShaderValueType::Float4 );
    EXPECT_EQ( params[0].Widget, ShaderParamWidget::Color );
    EXPECT_FLOAT_EQ( params[0].Default.x, 0.8f );
    EXPECT_FLOAT_EQ( params[0].Default.w, 1.0f );

    EXPECT_EQ( params[1].Name, "Tiling" );
    EXPECT_EQ( params[1].Type, ShaderValueType::Float );
    ASSERT_TRUE( params[1].Min.has_value() );
    EXPECT_FLOAT_EQ( *params[1].Min, 0.25f );
    EXPECT_FLOAT_EQ( *params[1].Max, 64.0f );
    EXPECT_EQ( params[1].Widget, ShaderParamWidget::Slider );
    EXPECT_FLOAT_EQ( params[1].Default.x, 4.0f );

    EXPECT_TRUE( params[2].IsTexture );
    EXPECT_EQ( params[2].DefaultTexture, "white" );
}

TEST( DShaderParser, ParsesRenderState )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& s = res.GetValue().Meta.State;

    EXPECT_EQ( s.Cull, StateCull::Back );
    EXPECT_EQ( s.DepthCompare, StateCompare::LessOrEqual );
    EXPECT_EQ( s.DepthTest, true );
    EXPECT_EQ( s.DepthWrite, true );
    EXPECT_EQ( s.Blend, true );
}

TEST( DShaderParser, GeneratesMaterialUBAndSamplersInFragmentOnly )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& stages = res.GetValue().Stages;

    const auto& frag = stages.at( ShaderStage::Fragment );
    EXPECT_NE( frag.find( "layout( binding = 1 ) uniform MaterialUB" ), std::string::npos );
    EXPECT_NE( frag.find( "vec4 Color;" ), std::string::npos );
    EXPECT_NE( frag.find( "float Tiling;" ), std::string::npos );
    EXPECT_NE( frag.find( "layout( binding = 2 ) uniform sampler2D u_AlbedoTex;" ), std::string::npos );
    EXPECT_NE( frag.find( "u_Material;" ), std::string::npos );

    const auto& vert = stages.at( ShaderStage::Vertex );
    EXPECT_EQ( vert.find( "MaterialUB" ), std::string::npos );
    EXPECT_EQ( vert.find( "sampler2D" ), std::string::npos );

    // Every stage gets the version header exactly once, as the first line.
    EXPECT_EQ( frag.rfind( "#version 450", 0 ), 0u );
    EXPECT_EQ( vert.rfind( "#version 450", 0 ), 0u );
}

TEST( DShaderParser, EmitsLineDirectivesForErrorMapping )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    EXPECT_NE( res.GetValue().Stages.at( ShaderStage::Fragment ).find( "#line " ), std::string::npos );
}

TEST( DShaderParser, MetadataOnlyModeGeneratesNothing )
{
    const char* src = R"(
Shader "Manual"
{
    Properties
    {
        Color Tint ("Tint") = (1, 1, 1, 1)
    }
    Fragment
    {
        void main() {}
    }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    EXPECT_EQ( res.GetValue().Stages.at( ShaderStage::Fragment ).find( "MaterialUB" ), std::string::npos );
    ASSERT_EQ( res.GetValue().Meta.Params.size(), 1u );
}

TEST( DShaderParser, IncludeBlockIsSharedAcrossStages )
{
    const char* src = R"(
Shader "WithInclude"
{
    Include
    {
        float shared_helper( float x ) { return x * 2.0; }
    }
    Vertex   { void main() { gl_Position = vec4( shared_helper( 1.0 ) ); } }
    Fragment { layout( location = 0 ) out vec4 c; void main() { c = vec4( shared_helper( 2.0 ) ); } }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    EXPECT_NE( res.GetValue().Stages.at( ShaderStage::Vertex ).find( "shared_helper" ), std::string::npos );
    EXPECT_NE( res.GetValue().Stages.at( ShaderStage::Fragment ).find( "shared_helper" ), std::string::npos );
}

TEST( DShaderParser, ErrorsCarryLineNumbers )
{
    // Unknown State command on line 5.
    const char* src = "Shader \"Broken\"\n{\n    State\n    {\n        Frobnicate On\n    }\n}\n";
    auto        res = DShaderParser::Parse( src );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "line 5" ), std::string::npos ) << res.GetError();
    EXPECT_NE( res.GetError().find( "Frobnicate" ), std::string::npos ) << res.GetError();
}

TEST( DShaderParser, RejectsVersionInStageBlock )
{
    const char* src = R"(
Shader "Versioned"
{
    Fragment
    {
        #version 450
        void main() {}
    }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "#version" ), std::string::npos );
}

TEST( DShaderParser, RejectsMissingStages )
{
    auto res = DShaderParser::Parse( "Shader \"Empty\" { Domain Surface }" );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "no stage blocks" ), std::string::npos );
}

TEST( DShaderParser, BracesInsideGlslCommentsDoNotBreakBlocks )
{
    const char* src = R"(
Shader "Tricky"
{
    Fragment
    {
        // a stray } in a comment
        /* and another } here */
        void main() {}
    }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    EXPECT_NE( res.GetValue().Stages.at( ShaderStage::Fragment ).find( "void main()" ), std::string::npos );
}

// ─── v2: multi-pass ─────────────────────────────────────────────────────────────

namespace
{
    const char* kMultiPass = R"(
Shader "Lit"
{
    Domain Surface

    Properties Binding(1)
    {
        Color Tint ("Tint") = (1, 1, 1, 1)
    }

    State
    {
        Cull Back
        ZTest Less
        Blend On
    }

    Vertex   { void main() { gl_Position = vec4(0.0); } }
    Fragment { layout( location = 0 ) out vec4 c; void main() { c = u_Material.Tint; } }

    Pass "Shadow"
    {
        State
        {
            Cull Front
            Blend Off
        }
        Vertex { void main() { gl_Position = vec4(1.0); } }
    }

    Pass "Outline"
    {
        Vertex   { void main() { gl_Position = vec4(2.0); } }
        Fragment { layout( location = 0 ) out vec4 c; void main() { c = vec4(1.0); } }
    }
}
)";
}

TEST( DShaderParserPasses, CollectsAllPasses )
{
    auto res = DShaderParser::Parse( kMultiPass );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& p = res.GetValue();

    ASSERT_EQ( p.Passes.size(), 3u );
    EXPECT_EQ( p.Passes[0].Name, "" ); // default program first
    EXPECT_EQ( p.Passes[1].Name, "Shadow" );
    EXPECT_EQ( p.Passes[2].Name, "Outline" );

    // The meta advertises the NAMED passes only (the default one is the shader itself).
    ASSERT_EQ( p.Meta.PassNames.size(), 2u );
    EXPECT_EQ( p.Meta.PassNames[0], "Shadow" );
    EXPECT_EQ( p.Meta.PassNames[1], "Outline" );

    EXPECT_NE( p.FindPass( "" ), nullptr );
    EXPECT_NE( p.FindPass( "Shadow" ), nullptr );
    EXPECT_EQ( p.FindPass( "Missing" ), nullptr );
}

TEST( DShaderParserPasses, DefaultProgramKeepsTopLevelStagesAndState )
{
    auto res = DShaderParser::Parse( kMultiPass );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& p = res.GetValue();

    // result.Stages / Meta.State mirror the default pass (backward compatibility).
    EXPECT_EQ( p.Stages.size(), p.Passes[0].Stages.size() );
    EXPECT_EQ( p.Meta.State.Cull, StateCull::Back );
    EXPECT_EQ( p.Meta.State.Blend, true );
}

TEST( DShaderParserPasses, PassStateInheritsAndOverrides )
{
    auto res = DShaderParser::Parse( kMultiPass );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto* shadow = res.GetValue().FindPass( "Shadow" );
    ASSERT_NE( shadow, nullptr );

    EXPECT_EQ( shadow->State.Cull, StateCull::Front );        // overridden
    EXPECT_EQ( shadow->State.Blend, false );                  // overridden (On -> Off)
    EXPECT_EQ( shadow->State.DepthCompare, StateCompare::Less ); // inherited from file State
    EXPECT_EQ( shadow->State.DepthTest, true );                  // inherited

    const auto* outline = res.GetValue().FindPass( "Outline" );
    ASSERT_NE( outline, nullptr );
    EXPECT_EQ( outline->State.Cull, StateCull::Back ); // fully inherited (no overrides)
    EXPECT_EQ( outline->State.Blend, true );
}

TEST( DShaderParserPasses, PassStagesAreIndependent )
{
    auto res = DShaderParser::Parse( kMultiPass );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& p = res.GetValue();

    const auto* shadow = p.FindPass( "Shadow" );
    ASSERT_NE( shadow, nullptr );
    ASSERT_EQ( shadow->Stages.size(), 1u ); // vertex-only depth pass
    EXPECT_TRUE( shadow->Stages.count( ShaderStage::Vertex ) );
    EXPECT_NE( shadow->Stages.at( ShaderStage::Vertex ).find( "vec4(1.0)" ), std::string::npos );

    // Auto-generated resources reach pass fragments too.
    const auto* outline = p.FindPass( "Outline" );
    ASSERT_NE( outline, nullptr );
    EXPECT_NE( outline->Stages.at( ShaderStage::Fragment ).find( "MaterialUB" ), std::string::npos );
}

TEST( DShaderParserPasses, PassOnlyShaderUsesFirstPassAsDefault )
{
    const char* src = R"(
Shader "PassOnly"
{
    Pass "Main"
    {
        State  { Cull None }
        Vertex { void main() { gl_Position = vec4(0.0); } }
    }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& p = res.GetValue();

    ASSERT_EQ( p.Passes.size(), 1u );
    EXPECT_EQ( p.Passes[0].Name, "Main" );
    EXPECT_EQ( p.Stages.size(), 1u ); // first pass doubles as the default program
    EXPECT_EQ( p.Meta.State.Cull, StateCull::None );
    ASSERT_EQ( p.Meta.PassNames.size(), 1u );
}

TEST( DShaderParserPasses, RejectsDuplicatePassNames )
{
    const char* src = R"(
Shader "Dup"
{
    Pass "A" { Vertex { void main() {} } }
    Pass "A" { Vertex { void main() {} } }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "duplicate Pass" ), std::string::npos ) << res.GetError();
}

TEST( DShaderParserPasses, RejectsEmptyPass )
{
    const char* src = R"(
Shader "Empty"
{
    Pass "NoStages" { State { Cull None } }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "no stage blocks" ), std::string::npos ) << res.GetError();
}

TEST( DShaderParserPasses, RejectsUnknownPassSection )
{
    const char* src = R"(
Shader "Bad"
{
    Pass "P"
    {
        Properties { Float X ("X") }
        Vertex { void main() {} }
    }
}
)";
    auto res = DShaderParser::Parse( src );
    ASSERT_FALSE( res.IsSuccess() );
    EXPECT_NE( res.GetError().find( "unknown Pass section" ), std::string::npos ) << res.GetError();
}

TEST( DShaderParserPasses, RejectsUnnamedPass )
{
    auto res = DShaderParser::Parse( "Shader \"X\" { Pass \"\" { Vertex { void main() {} } } }" );
    ASSERT_FALSE( res.IsSuccess() );
}

TEST( DShaderParserPasses, SinglePassShaderHasNoPassNames )
{
    auto res = DShaderParser::Parse( kUnlit );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    EXPECT_TRUE( res.GetValue().Meta.PassNames.empty() );
    ASSERT_EQ( res.GetValue().Passes.size(), 1u );
    EXPECT_EQ( res.GetValue().Passes[0].Name, "" );
}

TEST( DShaderParser, TranslatesLayoutSugar )
{
    const char* kSugar = R"(
Shader "Sugar"
{
    Domain Surface
    Vertex
    {
        In(0) vec3 a_Position;
        Out(1) vec2 v_UV;
        PushConstant Push { mat4 M; } pc;
        void main() { gl_Position = pc.M * vec4(a_Position, 1.0); v_UV = vec2(0.0); }
    }
    Fragment
    {
        Uniform(3) sampler2D u_Tex;
        In(1) vec2 v_UV;
        Out(0) vec4 o;
        void main() { o = texture(u_Tex, v_UV); }
    }
}
)";
    auto res = DShaderParser::Parse( kSugar );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();

    const auto& stages = res.GetValue().Stages;
    ASSERT_TRUE( stages.count( ShaderStage::Vertex ) );
    ASSERT_TRUE( stages.count( ShaderStage::Fragment ) );
    const std::string& vs = stages.at( ShaderStage::Vertex );
    const std::string& fs = stages.at( ShaderStage::Fragment );

    EXPECT_NE( vs.find( "layout(location = 0) in vec3 a_Position;" ), std::string::npos );
    EXPECT_NE( vs.find( "layout(location = 1) out vec2 v_UV;" ), std::string::npos );
    EXPECT_NE( vs.find( "layout(push_constant) uniform Push { mat4 M; } pc;" ), std::string::npos );
    EXPECT_NE( fs.find( "layout(binding = 3) uniform sampler2D u_Tex;" ), std::string::npos );
    EXPECT_NE( fs.find( "layout(location = 0) out vec4 o;" ), std::string::npos );

    // The sugar keywords must be fully consumed (no leftover In(/Out(/Uniform(/PushConstant).
    EXPECT_EQ( vs.find( "In(0)" ), std::string::npos );
    EXPECT_EQ( fs.find( "Uniform(3)" ), std::string::npos );
}

TEST( DShaderParser, ParsesBlendFactorsAndStencil )
{
    const char* kSrc = R"(
Shader "BS"
{
    State
    {
        Blend One OneMinusSrcAlpha
        Stencil Equal 1 Keep Replace Keep
    }
    Vertex   { void main() { gl_Position = vec4(0.0); } }
    Fragment { Out(0) vec4 o; void main() { o = vec4(1.0); } }
}
)";
    auto res = DShaderParser::Parse( kSrc );
    ASSERT_TRUE( res.IsSuccess() ) << res.GetError();
    const auto& s = res.GetValue().Meta.State;

    ASSERT_TRUE( s.Blend.has_value() );
    EXPECT_TRUE( *s.Blend );
    ASSERT_TRUE( s.BlendSrc.has_value() );
    ASSERT_TRUE( s.BlendDst.has_value() );
    EXPECT_EQ( *s.BlendSrc, StateBlendFactor::One );
    EXPECT_EQ( *s.BlendDst, StateBlendFactor::OneMinusSrcAlpha );

    ASSERT_TRUE( s.StencilTest.value_or( false ) );
    EXPECT_EQ( *s.StencilCompare, StateCompare::Equal );
    EXPECT_EQ( *s.StencilRef, 1u );
    EXPECT_EQ( *s.StencilPass, StateStencilOp::Replace );
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
