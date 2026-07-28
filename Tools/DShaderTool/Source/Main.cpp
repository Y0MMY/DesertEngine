// DShaderTool — offline linter for Desert Shader Language (.shader) files.
//
//   DShaderTool <file|dir> [<file|dir> ...]
//
// Recursively collects *.shader under every argument, runs the ENGINE's DShaderParser over each and
// reports every parse error with its file. Exit code 0 = all parsed, 1 = at least one failure.
// Files in the legacy #pragma-program format (not DSL) are counted as skipped, not errors.
//
// The parser is the same translation unit the engine/editor uses (compiled in via premake) — this
// tool can never drift from what the runtime actually accepts.

#include <Engine/Core/ShaderCompiler/DShader/DShaderParser.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::string ReadFile( const fs::path& path )
    {
        std::ifstream in( path, std::ios::in | std::ios::binary );
        if ( !in )
            return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    void Collect( const fs::path& root, std::vector<fs::path>& out )
    {
        std::error_code ec;
        if ( fs::is_regular_file( root, ec ) )
        {
            if ( root.extension() == ".shader" )
                out.push_back( root );
            return;
        }
        for ( auto it = fs::recursive_directory_iterator( root, ec );
              it != fs::recursive_directory_iterator(); it.increment( ec ) )
        {
            if ( !ec && it->is_regular_file() && it->path().extension() == ".shader" )
                out.push_back( it->path() );
        }
    }
} // namespace

int main( int argc, char** argv )
{
    if ( argc < 2 )
    {
        std::fprintf( stderr, "Usage: DShaderTool <file|dir> [<file|dir> ...]\n" );
        return 2;
    }

    std::vector<fs::path> files;
    for ( int i = 1; i < argc; ++i )
        Collect( argv[i], files );

    if ( files.empty() )
    {
        std::fprintf( stderr, "DShaderTool: no .shader files found under the given paths\n" );
        return 2;
    }

    int parsed = 0, skipped = 0, failed = 0;
    for ( const auto& file : files )
    {
        const std::string source = ReadFile( file );
        if ( source.empty() )
        {
            std::printf( "FAIL  %s: cannot read file\n", file.string().c_str() );
            ++failed;
            continue;
        }

        using Desert::Core::Preprocess::DShaderParser;
        if ( !DShaderParser::IsDShader( source ) )
        {
            ++skipped; // legacy #pragma-program format — not DSL, nothing to lint here
            continue;
        }

        const auto result = DShaderParser::Parse( source );
        if ( result.IsSuccess() )
        {
            ++parsed;
        }
        else
        {
            std::printf( "FAIL  %s: %s\n", file.string().c_str(), result.GetError().c_str() );
            ++failed;
        }
    }

    std::printf( "DShaderTool: %d parsed, %d skipped (legacy format), %d failed — %zu file(s) total\n",
                 parsed, skipped, failed, files.size() );
    return failed == 0 ? 0 : 1;
}
