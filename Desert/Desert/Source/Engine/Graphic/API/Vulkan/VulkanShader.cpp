#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

#include <shaderc/shaderc.hpp>
#include <vulkan/vulkan.h>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        void CreateDirectoriesIfNoExists()
        {
            bool exist = Common::Utils::FileSystem::Exists( Common::Constants::Path::SHADERDIR_PATH );
            if ( !exist )
            {
                Common::Utils::FileSystem::CreateDirectory( Common::Constants::Path::SHADERDIR_PATH );
            }
        }

        shaderc_shader_kind ShaderStageToShaderC( Core::Formats::ShaderStage shaderType )
        {
            switch ( shaderType )
            {
                case Desert::Core::Formats::ShaderStage::Vertex:
                    return shaderc_vertex_shader;
                case Desert::Core::Formats::ShaderStage::Fragment:
                    return shaderc_fragment_shader;
                case Desert::Core::Formats::ShaderStage::Compute:
                    return shaderc_compute_shader;
            }
            return (shaderc_shader_kind)0;
        }

        Common::Result<std::vector<uint32_t>> Compile( const Core::Formats::ShaderStage stage,
                                                       const std::string                shaderSource,
                                                       const std::string&               shaderPath )
        {
            static shaderc::Compiler compiler;
            shaderc::CompileOptions  shaderCOptions;

            shaderCOptions.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1 );
            shaderCOptions.SetWarningsAsErrors();

            // Compile shader
            const shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
                 shaderSource, ShaderStageToShaderC( stage ), shaderPath.c_str(), shaderCOptions );

            if ( module.GetCompilationStatus() != shaderc_compilation_status_success )
            {

                return Common::MakeFormattedError<std::vector<uint32_t>>(
                     "{}While compiling shader file: {} \nAt stage: {}", module.GetErrorMessage(), shaderPath,
                     "TODO" );
            }

            return Common::MakeSuccess( std::vector<uint32_t>( module.begin(), module.end() ) );
        }

        void CreateShaderModule( const Core::Formats::ShaderStage& stage, const std::vector<uint32_t>& shaderData,
                                 const std::filesystem::path& filepath )
        {
            VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

            std::string              moduleName;
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = shaderData.size() * sizeof( uint32_t );
            moduleCreateInfo.pCode    = shaderData.data();

            VkShaderModule shaderModule;
            VK_CHECK_RESULT( vkCreateShaderModule( device, &moduleCreateInfo, NULL, &shaderModule ) );
            VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_SHADER_MODULE,
                                              fmt::format( "{}:{}", filepath.filename().string(), "TODO" ), shaderModule );
        }

    } // namespace Utils

    VulkanShader::VulkanShader( const std::filesystem::path& path ) : m_ShaderPath( path )
    {
        Utils::CreateDirectoriesIfNoExists();

        Reload();
    }

    Common::BoolResult VulkanShader::Reload() // also load
    {
        const auto shaderContent    = Common::Utils::FileSystem::ReadFileContent( m_ShaderPath );
        const auto shadersWithStage = Core::Preprocess::Shader::PreProcess( shaderContent );

        for ( auto [stage, source] : shadersWithStage )
        {
            const auto result = Utils::Compile( stage, source, m_ShaderPath.string() );

            if ( !result.IsSuccess() )
            {
                return Common::MakeError<bool>( result.GetError() );
            }

            Utils::CreateShaderModule( stage, result.GetValue(), m_ShaderPath );
        }

        LOG_INFO("The shader {} was created or reloaded", m_ShaderPath.filename().string());
    }

} // namespace Desert::Graphic::API::Vulkan