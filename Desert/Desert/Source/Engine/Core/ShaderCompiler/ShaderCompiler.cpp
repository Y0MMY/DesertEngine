#include "ShaderCompiler.hpp"
#include <Engine/Core/ShaderCompiler/Includer/ShaderIncluder.hpp>
#include <Engine/Core/ShaderCompiler/ShaderCacheKey.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <shaderc/shaderc.hpp>
#include <Common/Core/Core.hpp>
#include <Common/Core/Constants.hpp>
#include <Common/Utilities/FileSystem.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>

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
        // (recursively) included file — Core::ComputeShaderCacheKey, shared with the hot-reload
        // watcher so "what this stage is made of" has exactly one definition. Cache artifacts live in
        // Cooked/ShaderCache/<key>.spv next to the other cooked assets.

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
        const uint64_t key = ComputeShaderCacheKey( stage, source, shaderPath );

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
