#include "ShaderCompiler.hpp"
#include <Engine/Core/ShaderCompiler/Includer/ShaderIncluder.hpp>
#include <Engine/Graphic/Shader.hpp>

#include <shaderc/shaderc.hpp>
#include <Common/Core/Core.hpp>
#include <format>

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
    }

    Common::ResultStr<std::vector<uint32_t>> ShaderCompiler::CompileGLSLToSPIRV( 
        Formats::ShaderStage stage, 
        const std::string& source, 
        const std::string& shaderPath )
    {
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

        return Common::MakeSuccess( std::vector<uint32_t>( result.begin(), result.end() ) );
    }

} // namespace Desert::Core
