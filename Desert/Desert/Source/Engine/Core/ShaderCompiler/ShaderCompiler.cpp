#include "ShaderCompiler.hpp"
#include <Engine/Core/ShaderCompiler/Includer/ShaderIncluder.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <shaderc/shaderc.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <unordered_set>

namespace Desert::Core
{
    namespace
    {
        static shaderc_shader_kind ConvertShaderStage( Formats::ShaderStage stage )
        {
            switch ( stage )
            {
                case Formats::ShaderStage::Vertex:         return shaderc_vertex_shader;
                case Formats::ShaderStage::TessControl:    return shaderc_tess_control_shader;
                case Formats::ShaderStage::TessEvaluation: return shaderc_tess_evaluation_shader;
                case Formats::ShaderStage::Fragment:       return shaderc_fragment_shader;
                case Formats::ShaderStage::Compute:        return shaderc_compute_shader;
                default:
                    DESERT_VERIFY( false, "Unsupported shader stage for compilation" );
                    return (shaderc_shader_kind)0;
            }
        }

        // ---- SPIR-V disk cache -------------------------------------------------------------------
        // Content-addressed: the key hashes the assembled stage source PLUS the content of every
        // (recursively) included file, so editing an include invalidates all shaders using it.
        // Cache artifacts live in Cooked/ShaderCache/<key>.spv next to the other cooked assets.

        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime  = 1099511628211ull;

        void FnvMix( uint64_t& h, std::string_view data )
        {
            for ( unsigned char c : data )
            {
                h ^= c;
                h *= kFnvPrime;
            }
        }

        // Mirrors ShaderIncluder resolution: #include "x" -> relative to the including file,
        // #include <x> -> relative to the engine shader root. FileSystem is VFS-aware, so packaged
        // games hash pak contents identically.
        void HashIncludesRecursive( const std::string& source, const std::filesystem::path& requestingFile,
                                    uint64_t& h, std::unordered_set<std::string>& visited, int depth = 0 )
        {
            if ( depth > 32 )
                return;

            size_t pos = 0;
            while ( ( pos = source.find( "#include", pos ) ) != std::string::npos )
            {
                const size_t lineEnd = source.find( '\n', pos );
                const std::string line =
                     source.substr( pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos );
                pos += 8;

                const size_t qa = line.find_first_of( "\"<" );
                if ( qa == std::string::npos )
                    continue;
                const char   closer = line[qa] == '"' ? '"' : '>';
                const size_t qb     = line.find( closer, qa + 1 );
                if ( qb == std::string::npos )
                    continue;
                const std::string name = line.substr( qa + 1, qb - qa - 1 );

                const std::filesystem::path full =
                     ( line[qa] == '"'
                            ? requestingFile.parent_path() / name
                            : Common::Constants::Path::SHADERDIR_PATH / name )
                          .lexically_normal();

                const std::string key = full.generic_string();
                if ( !visited.insert( key ).second )
                    continue;
                if ( !Common::Utils::FileSystem::Exists( full ) )
                    continue;

                const std::string content = Common::Utils::FileSystem::ReadFileContent( full );
                FnvMix( h, key );
                FnvMix( h, content );
                HashIncludesRecursive( content, full, h, visited, depth + 1 );
            }
        }

        std::filesystem::path CachePathForKey( uint64_t key )
        {
            return Common::Constants::Path::COOKED_PATH / "ShaderCache" /
                   std::format( "{:016x}.spv", key );
        }

        std::optional<std::vector<uint32_t>> TryLoadCachedSpirv( uint64_t key )
        {
            const auto path = CachePathForKey( key );
            std::error_code ec;
            const auto size = std::filesystem::file_size( path, ec );
            if ( ec || size == 0 || ( size % sizeof( uint32_t ) ) != 0 )
                return std::nullopt;

            std::ifstream in( path, std::ios::binary );
            if ( !in )
                return std::nullopt;
            std::vector<uint32_t> words( size / sizeof( uint32_t ) );
            in.read( reinterpret_cast<char*>( words.data() ), static_cast<std::streamsize>( size ) );
            if ( !in )
                return std::nullopt;
            return words;
        }

        void StoreCachedSpirv( uint64_t key, const std::vector<uint32_t>& spirv )
        {
            const auto path = CachePathForKey( key );
            std::error_code ec;
            std::filesystem::create_directories( path.parent_path(), ec );
            std::ofstream out( path, std::ios::binary | std::ios::trunc );
            if ( !out ) // read-only install (e.g. inside an .app bundle) — cache is best-effort
                return;
            out.write( reinterpret_cast<const char*>( spirv.data() ),
                       static_cast<std::streamsize>( spirv.size() * sizeof( uint32_t ) ) );
        }
    }

    Common::ResultStr<std::vector<uint32_t>> ShaderCompiler::CompileGLSLToSPIRV( 
        Formats::ShaderStage stage, 
        const std::string& source, 
        const std::string& shaderPath )
    {
        // Cache key: stage + compile-options fingerprint + assembled source + every included file's
        // content (recursive). Content-addressed, so any edit produces a fresh key — no mtime races.
        uint64_t key = kFnvOffset;
        FnvMix( key, "vulkan1.1|v1" );
#ifdef DESERT_CONFIG_DEBUG
        FnvMix( key, "|debuginfo" ); // debug info changes the binary — keep configs apart
#endif
        key ^= static_cast<uint64_t>( stage );
        key *= kFnvPrime;
        FnvMix( key, source );
        {
            std::unordered_set<std::string> visited;
            HashIncludesRecursive( source, shaderPath, key, visited );
        }

        if ( auto cached = TryLoadCachedSpirv( key ) )
            return Common::MakeSuccess( std::move( *cached ) );

        static shaderc::Compiler compiler;
        shaderc::CompileOptions  options;

        options.SetIncluder( std::make_unique<ShaderIncluder>( shaderPath ) );
        options.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1 );
        options.SetWarningsAsErrors();

#ifdef DESERT_CONFIG_DEBUG
        options.SetGenerateDebugInfo();
#endif

        const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
             source, ConvertShaderStage( stage ), shaderPath.c_str(), options );

        if ( result.GetCompilationStatus() != shaderc_compilation_status_success )
        {
            std::string errorMsg = std::format( "Shader Compilation Error ({}): {}\nFile: {}",
                                               Graphic::Shader::GetStringShaderStage( stage ),
                                               result.GetErrorMessage(),
                                               shaderPath );
            LOG_ERROR( errorMsg );
            return Common::MakeError<std::vector<uint32_t>>( errorMsg );
        }

        std::vector<uint32_t> spirv( result.begin(), result.end() );
        StoreCachedSpirv( key, spirv );
        return Common::MakeSuccess( std::move( spirv ) );
    }

} // namespace Desert::Core
