#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanHelper.hpp>

#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

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

        VkShaderStageFlagBits ShaderStageToVkShader( Core::Formats::ShaderStage shaderType )
        {
            switch ( shaderType )
            {
                case Desert::Core::Formats::ShaderStage::Vertex:
                    return VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
                case Desert::Core::Formats::ShaderStage::Fragment:
                    return VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
                case Desert::Core::Formats::ShaderStage::Compute:
                    return VkShaderStageFlagBits::VK_SHADER_STAGE_COMPUTE_BIT;
            }
            return (VkShaderStageFlagBits)0;
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
                LOG_ERROR( "{}While compiling shader file: {} \nAt stage: {}", module.GetErrorMessage(),
                           shaderPath, "TODO" );
                return Common::MakeFormattedError<std::vector<uint32_t>>(
                     "{}While compiling shader file: {} \nAt stage: {}", module.GetErrorMessage(), shaderPath,
                     "TODO" );
            }

            return Common::MakeSuccess( std::vector<uint32_t>( module.begin(), module.end() ) );
        }

        Common::BoolResult CreateShaderModule( const Core::Formats::ShaderStage&             stage,
                                               const std::vector<uint32_t>&                  shaderData,
                                               const std::filesystem::path&                  filepath,
                                               std::vector<VkPipelineShaderStageCreateInfo>& writeCreateInfo )
        {
            VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

            std::string              moduleName;
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = shaderData.size() * sizeof( uint32_t );
            moduleCreateInfo.pCode    = shaderData.data();

            VkShaderModule shaderModule;
            VK_RETURN_RESULT_IF_FALSE_TYPE(
                 bool, vkCreateShaderModule( device, &moduleCreateInfo, NULL, &shaderModule ) );
            VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_SHADER_MODULE,
                                              fmt::format( "{}:{}", filepath.filename().string(), "TODO" ),
                                              shaderModule );

            VkPipelineShaderStageCreateInfo& shaderStage = writeCreateInfo.emplace_back();
            shaderStage.sType                            = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shaderStage.stage                            = Utils::ShaderStageToVkShader( stage );
            shaderStage.module                           = shaderModule;
            shaderStage.pName                            = "main";

            return Common::MakeSuccess( true );
        }

    } // namespace Utils

    VulkanShader::VulkanShader( const std::filesystem::path& path ) : m_ShaderPath( path )
    {
        Utils::CreateDirectoriesIfNoExists(); // TODO: move to a better location

        Reload();
    }

    static std::unordered_map<uint32_t, std::unordered_map<uint32_t, ShaderResource::UniformBuffer>>
         s_UniformBuffers; // set -> binding point -> buffer

    Common::BoolResult VulkanShader::Reload() // also load
    {
        m_PipelineShaderStageCreateInfos.clear();

        const auto shaderContent    = Common::Utils::FileSystem::ReadFileContent( m_ShaderPath );
        const auto shadersWithStage = Core::Preprocess::Shader::PreProcess( shaderContent );

        for ( auto [stage, source] : shadersWithStage )
        {
            const auto compileResult = Utils::Compile( stage, source, m_ShaderPath.string() );

            if ( !compileResult.IsSuccess() )
            {
                return Common::MakeError<bool>( compileResult.GetError() );
            }

            const auto createShaderModuleResult = Utils::CreateShaderModule(
                 stage, compileResult.GetValue(), m_ShaderPath, m_PipelineShaderStageCreateInfos );

            if ( !createShaderModuleResult.IsSuccess() )
            {
                return Common::MakeError<bool>( createShaderModuleResult.GetError() );
            }

            m_ReflectionData.ShaderDescriptorSets.clear();

            Reflect( Utils::ShaderStageToVkShader( stage ), compileResult.GetValue() );
            const auto createDescriptorsResult = CreateDescriptors();
            if ( !createDescriptorsResult.IsSuccess() )
            {
                return Common::MakeError<bool>( createDescriptorsResult.GetError() );
            }
        }

        LOG_INFO( "The shader {} was created or reloaded", m_ShaderPath.filename().string() );
    }

    void VulkanShader::Reflect( VkShaderStageFlagBits flag, const std::vector<uint32_t>& spirvBinary )
    {
        LOG_TRACE( "===========================" );
        LOG_TRACE( " Vulkan Shader Reflection" );
        LOG_TRACE( "===========================" );

        spirv_cross::Compiler compiler( spirvBinary );
        auto                  resources = compiler.get_shader_resources();

        LOG_TRACE( "Uniform Buffers: " );

        for ( const auto& resource : resources.uniform_buffers )
        {
            auto activeBuffers = compiler.get_active_buffer_ranges( resource.id );
            if ( activeBuffers.size() )
            {
                const auto& name          = resource.name;
                auto&       bufferType    = compiler.get_type( resource.base_type_id );
                int         memberCount   = (uint32_t)bufferType.member_types.size();
                uint32_t    binding       = compiler.get_decoration( resource.id, spv::DecorationBinding );
                uint32_t    descriptorSet = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
                uint32_t    size          = (uint32_t)compiler.get_declared_struct_size( bufferType );

                if ( descriptorSet >= m_ReflectionData.ShaderDescriptorSets.size() )
                    m_ReflectionData.ShaderDescriptorSets.resize( descriptorSet + 1 );

                ShaderResource::ShaderDescriptorSet& shaderDescriptorSet =
                     m_ReflectionData.ShaderDescriptorSets[descriptorSet];
                if ( s_UniformBuffers[descriptorSet].find( binding ) == s_UniformBuffers[descriptorSet].end() )
                {
                    ShaderResource::UniformBuffer uniformBuffer;
                    uniformBuffer.BindingPoint                    = binding;
                    uniformBuffer.Size                            = size;
                    uniformBuffer.Name                            = name;
                    uniformBuffer.ShaderStage                     = VK_SHADER_STAGE_ALL;
                    s_UniformBuffers.at( descriptorSet )[binding] = uniformBuffer;
                }
                else
                {
                    ShaderResource::UniformBuffer& uniformBuffer =
                         s_UniformBuffers.at( descriptorSet ).at( binding );
                    if ( size > uniformBuffer.Size )
                        uniformBuffer.Size = size;
                }
                shaderDescriptorSet.UniformBuffers[binding] = s_UniformBuffers.at( descriptorSet ).at( binding );

                LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
                LOG_TRACE( "  Member Count: {0}", memberCount );
                LOG_TRACE( "  Size: {0}", size );
                LOG_TRACE( "-------------------" );
            }
        }
    }

    Common::BoolResult VulkanShader::CreateDescriptors()
    {
        const auto result = CreateDescriptorsLayout();
        if ( !result.IsSuccess() )
        {
            return Common::MakeError<bool>( result.GetError() );
        }

        return Common::MakeSuccess( true );
    }

    Common::BoolResult VulkanShader::CreateDescriptorsLayout()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        for ( uint32_t set = 0; set < m_ReflectionData.ShaderDescriptorSets.size(); set++ )
        {
            auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];
            for ( auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers )
            {
                VkDescriptorSetLayoutBinding& layoutBinding = layoutBindings.emplace_back();
                layoutBinding.descriptorType                = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                layoutBinding.descriptorCount               = 1;
                layoutBinding.stageFlags                    = uniformBuffer.ShaderStage;
                layoutBinding.pImmutableSamplers            = nullptr;
                layoutBinding.binding                       = binding;

                /* VkWriteDescriptorSet& set = shaderDescriptorSet.WriteDescriptorSets[uniformBuffer.Name];
                 set                       = {};
                 set.sType                 = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                 set.descriptorType        = layoutBinding.descriptorType;
                 set.descriptorCount       = 1;
                 set.dstBinding            = layoutBinding.binding;*/
            }

            VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
            descriptorLayout.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorLayout.pNext                           = nullptr;
            descriptorLayout.bindingCount                    = (uint32_t)( layoutBindings.size() );
            descriptorLayout.pBindings                       = layoutBindings.data();

            if ( set >= m_DescriptorSetLayouts.size() )
                m_DescriptorSetLayouts.resize( (size_t)( set + 1 ) );

            VK_RETURN_RESULT_IF_FALSE_TYPE( bool, vkCreateDescriptorSetLayout( device, &descriptorLayout, nullptr,
                                                                               &m_DescriptorSetLayouts[set] ) );
        }

        return Common::MakeSuccess( true );
    }

    std::vector<VkDescriptorSetLayout> VulkanShader::GetAllDescriptorSetLayouts()
    {
        return {};
    }

} // namespace Desert::Graphic::API::Vulkan