// What every .shader the editor ships must be true of, checked against the engine's OWN DSL parser.
//
// WHY THIS SUITE EXISTS. `Unlit.shader` carried a `Pass "Depth"` for as long as the DSL had passes. It
// was commented as the shadow variant and it could not have been one: it declared NO fragment stage,
// while every attachment this engine renders into — the cascade included, which is a colour R32F target
// `Shadow.shader` WRITES with `gl_FragCoord.z` — has to be written by a fragment shader. Nothing named
// it either: `ShaderService` registers each named pass as its own program under "<Shader>/<Pass>", and
// no line of C++ has ever asked for one. So it was a whole SPIR-V module compiled at every startup for
// a program no draw could reach, and the file said the opposite of the truth to anyone reading it.
//
// Both halves of that are relations between two places, which is why they are here rather than in a
// comment: a pass and the target it would write, and a pass and the code that would address it. Each
// side alone reads as correct, and nothing was checking that they agreed.
//
// The parser is compiled into the test, so what is asserted is the same parse the runtime performs.

#include <gtest/gtest.h>

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using Desert::Core::Formats::ShaderStage;
using Desert::Core::Preprocess::DShaderParser;
using Desert::Core::Preprocess::DShaderParseResult;

namespace
{
    struct ParsedShader
    {
        std::filesystem::path                 File;
        Common::ResultStr<DShaderParseResult> Parsed;
    };

    std::string ReadFile( const std::filesystem::path& path )
    {
        std::ifstream      in( path, std::ios::binary );
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    // Every .shader under Editor/Resources/Shaders/Programs, parsed ONCE and sorted, so a failure names
    // the same file on every machine and the whole suite pays for one walk rather than one per test
    // (assembling a stage un-sugars every line, which is not free over seventy-odd files). The root is
    // found by walking up from the test binary in build/Bin/Tests/<config>, as Tests/Engine/ShaderCacheKey
    // does.
    const std::vector<ParsedShader>& ShippedShaders()
    {
        static const std::vector<ParsedShader> shaders = []() -> std::vector<ParsedShader>
        {
            std::filesystem::path here = std::filesystem::current_path();
            for ( int up = 0; up < 8 && !std::filesystem::exists( here / "Editor" / "Resources" / "Shaders" );
                  ++up )
                here = here.parent_path();

            std::vector<std::filesystem::path> files;
            const auto                         root = here / "Editor" / "Resources" / "Shaders" / "Programs";
            if ( std::filesystem::exists( root ) )
                for ( const auto& entry : std::filesystem::recursive_directory_iterator( root ) )
                    if ( entry.is_regular_file() && entry.path().extension() == ".shader" )
                        files.push_back( entry.path() );
            std::sort( files.begin(), files.end() );

            std::vector<ParsedShader> out;
            out.reserve( files.size() );
            for ( const auto& file : files )
                out.push_back( { file, DShaderParser::Parse( ReadFile( file ) ) } );
            return out;
        }();
        return shaders;
    }
} // namespace

// The shipped shaders are data, and nothing else in the repository parses all of them: ShaderCacheKey
// reaches six by name and the editor reads the rest for the first time at startup. A syntax error in one
// is a shader that does not draw, reported only in a log nobody is reading.
TEST( ShippedShaderPasses, EveryShippedShaderParses )
{
    ASSERT_FALSE( ShippedShaders().empty() ) << "found no .shader files — the shader root was not located";

    for ( const auto& shader : ShippedShaders() )
        EXPECT_TRUE( shader.Parsed.IsSuccess() ) << shader.File.string() << ": " << shader.Parsed.GetError();
}

// THE RELATION: a pass that rasterizes writes an attachment, and only a fragment stage can write one.
// A vertex stage with no fragment stage beside it produces a program that runs and emits nothing —
// which is exactly what `Unlit/Depth` was, and why calling it a shadow caster was never true. Compute
// passes are exempt because they write through storage bindings instead.
TEST( ShippedShaderPasses, EveryRasterPassCanWriteWhatItRendersInto )
{
    for ( const auto& shader : ShippedShaders() )
    {
        ASSERT_TRUE( shader.Parsed.IsSuccess() ) << shader.File.string() << ": " << shader.Parsed.GetError();

        for ( const auto& pass : shader.Parsed.GetValue().Passes )
        {
            if ( !pass.Stages.count( ShaderStage::Vertex ) )
                continue;

            const std::string label = pass.Name.empty() ? std::string( "<default pass>" ) : pass.Name;
            EXPECT_TRUE( pass.Stages.count( ShaderStage::Fragment ) )
                 << shader.File.string() << ", pass " << label
                 << ": rasterizes but declares no fragment stage, so it writes nothing to the target it "
                    "is rendered into";
        }
    }
}

// THE OTHER RELATION: a named pass is a separate program, addressable only as "<Shader>/<Pass>" through
// ShaderService::GetByName — and no consumer in the engine asks for one. So a shipped shader that
// declares a named pass ships a program nothing can reach: it is compiled at every startup and drawn by
// nobody, and the reader of the file concludes it means something.
//
// WHEN THIS FAILS, THE FIX IS NOT TO RELAX IT. Either the pass has a consumer — then this test names the
// call site that must exist, and the pass belongs in an expected set added here beside it — or it has
// none, and the pass is the thing to delete.
TEST( ShippedShaderPasses, NoShippedShaderDeclaresAPassNothingCanAddress )
{
    for ( const auto& shader : ShippedShaders() )
    {
        ASSERT_TRUE( shader.Parsed.IsSuccess() ) << shader.File.string() << ": " << shader.Parsed.GetError();

        for ( const auto& passName : shader.Parsed.GetValue().Meta.PassNames )
            ADD_FAILURE() << shader.File.string() << ": declares Pass \"" << passName
                          << "\", which registers a program named \""
                          << shader.Parsed.GetValue().Name + "/" + passName
                          << "\" that no GetByName call in the engine ever asks for";
    }
}

int main( int argc, char** argv )
{
    testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
