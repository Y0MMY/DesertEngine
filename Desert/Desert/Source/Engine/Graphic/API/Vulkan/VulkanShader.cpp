#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/Core/Models/Shader.hpp>

#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace Utils
    {
        static bool IsArray( const spirv_cross::SPIRType& type )
        {
            return type.array.size() > 0;
        }

        static uint32_t GetArraySize( const spirv_cross::SPIRType& type )
        {
            if ( !IsArray( type ) )
                return 1;
            size_t arraySize = 1;
            for ( auto size : type.array )
            {
                arraySize *= size;
            }
            return arraySize;
        }

        Core::Formats::ShaderStage GetShaderStageFlagBitsFromSPV( const spv::ExecutionModel& executionModel )
        {
            switch ( executionModel )
            {
                case spv::ExecutionModelVertex:
                    return Core::Formats::ShaderStage::Vertex;
                case spv::ExecutionModelFragment:
                    return Core::Formats::ShaderStage::Fragment;
                case spv::ExecutionModelGLCompute:
                    return Core::Formats::ShaderStage::Compute;
            }

            return Core::Formats::ShaderStage::None;
        }

        VkShaderStageFlags GetVkShaderStageFlags( const Core::Formats::ShaderStage& stage )
        {
            switch ( stage )
            {
                case Core::Formats::ShaderStage::Vertex:
                    return VK_SHADER_STAGE_VERTEX_BIT;

                case Core::Formats::ShaderStage::Fragment:
                    return VK_SHADER_STAGE_FRAGMENT_BIT;

                case Core::Formats::ShaderStage::Compute:
                    return VK_SHADER_STAGE_COMPUTE_BIT;
            }
            return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        }

        std::vector<VkDescriptorPoolSize> GetDescriptorPoolSize(
             const std::unordered_map<SetPoint, ShaderResource::ShaderDescriptorSet>& shaderDescriptorSets )
        {
            std::vector<VkDescriptorPoolSize> poolSizes;
            uint32_t                          sets = shaderDescriptorSets.size();

            for ( SetPoint set = 0; set < sets; set++ )
            {
                const auto& uniformBuffers = shaderDescriptorSets.at( set ).UniformBuffers;
                if ( !uniformBuffers.empty() )
                {
                    auto& poolSize = poolSizes.emplace_back();

                    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    poolSize.descriptorCount = uniformBuffers.size() * sets;
                }

                const auto& imageSamplers = shaderDescriptorSets.at( set ).ImageSamplers;
                if ( !imageSamplers.empty() )
                {
                    auto& poolSize = poolSizes.emplace_back();

                    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    poolSize.descriptorCount = imageSamplers.size() * sets;
                }
            }

            return poolSizes;
        }

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

    Common::BoolResult VulkanShader::Reload() // also load
    {
        m_PipelineShaderStageCreateInfos.clear();

        const auto shaderContent    = Common::Utils::FileSystem::ReadFileContent( m_ShaderPath );
        const auto shadersWithStage = Core::Preprocess::Shader::PreProcess( shaderContent );

        m_ReflectionData.ShaderDescriptorSets.clear();

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

                auto& uniformBuffers = m_ReflectionData.ShaderDescriptorSets[descriptorSet].UniformBuffers;

                if ( uniformBuffers.find( binding ) == uniformBuffers.end() )
                {
                    Core::Models::UniformBuffer uniformBuffer;
                    uniformBuffer.BindingPoint = binding;
                    uniformBuffer.Size         = size;
                    uniformBuffer.Name         = name;
                    uniformBuffer.ShaderStage =
                         Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                    ShaderResource::ShaderDescriptorSet::UniformBufferPair insertPair = {
                         uniformBuffer, {} /*is defined later in CreateDescriptorSets*/ };

                    uniformBuffers.insert( { binding, insertPair } );
                }
                else
                {
                }

                LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
                LOG_TRACE( "  Member Count: {0}", memberCount );
                LOG_TRACE( "  Size: {0}", size );
                LOG_TRACE( "-------------------" );
            }
        }

        LOG_TRACE( "Images: " );
        for ( const auto& resource : resources.sampled_images )
        {
            const auto& name          = resource.name;
            uint32_t    binding       = compiler.get_decoration( resource.id, spv::DecorationBinding );
            uint32_t    descriptorSet = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            auto&       imageType     = compiler.get_type( resource.base_type_id );

            auto& imageSampler = m_ReflectionData.ShaderDescriptorSets[descriptorSet].ImageSamplers;

            if ( imageSampler.find( binding ) == imageSampler.end() )
            {
                uint32_t arraySize = 1;
                if ( Utils::IsArray( imageType ) )
                {
                    arraySize = Utils::GetArraySize( imageType );
                }

                Core::Models::ImageSampler newImageSampler;
                newImageSampler.BindingPoint = binding;
                newImageSampler.Name         = name;
                newImageSampler.ArraySize    = arraySize;
                newImageSampler.ShaderStage =
                     Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                ShaderResource::ShaderDescriptorSet::SamplerBufferPair insertPair = {
                     newImageSampler, {} /*is defined later in CreateDescriptorSets*/ };

                imageSampler.insert( { binding, insertPair } );
            }

            LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
        }

        LOG_TRACE( "Storage Buffers: " );
        for ( const auto& resource : resources.storage_buffers )
        {
            const auto& name          = resource.name;
            uint32_t    binding       = compiler.get_decoration( resource.id, spv::DecorationBinding );
            uint32_t    descriptorSet = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );

            auto& storageBuffers = m_ReflectionData.ShaderDescriptorSets[descriptorSet].StorageBuffers;

            if ( storageBuffers.find( binding ) == storageBuffers.end() )
            {
                Core::Models::StorageBuffer storageBuffer;
                storageBuffer.BindingPoint = binding;
                storageBuffer.Name         = name;
                storageBuffer.ShaderStage = Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                ShaderResource::ShaderDescriptorSet::StorageBufferPair insertPair = {
                     storageBuffer, {} /*is defined later in CreateDescriptorSets*/ };

                storageBuffers.insert( { binding, insertPair } );
            }

            LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
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

        uint32_t sets = m_ReflectionData.ShaderDescriptorSets.size();
        for ( SetPoint set = 0; set < sets; set++ )
        {
            auto& shaderDescriptorSet = m_ReflectionData.ShaderDescriptorSets[set];

            // Uniform buffer
            for ( const auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                layout.stageFlags         = Utils::GetVkShaderStageFlags( uniformBuffer.first.ShaderStage );
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1; // not array at now
            }

            // Samplers
            for ( const auto& [binding, sampler] : shaderDescriptorSet.ImageSamplers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                layout.stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT;
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1;
            }

            VkDescriptorSetLayoutCreateInfo layoutInfo = {};
            layoutInfo.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount                    = (uint32_t)layoutBindings.size();
            layoutInfo.pBindings                       = layoutBindings.data();

            if ( set >= m_DescriptorSetLayouts.size() )
            {
                m_DescriptorSetLayouts.resize( (size_t)( set + 1 ) );
            }

            VK_RETURN_RESULT_IF_FALSE_TYPE(
                 bool, vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &m_DescriptorSetLayouts[set] ) );
        }

        return Common::MakeSuccess( true );
    }

    std::vector<VkDescriptorSetLayout> VulkanShader::GetAllDescriptorSetLayouts()
    {
        return m_DescriptorSetLayouts;
    }

    VulkanShader::DescriptorSetInfo VulkanShader::AllocateDescriptorSets( uint32_t framesInFlight )
    {
        DescriptorSetInfo result;

        VkDevice    device    = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        const auto& poolSizes = Utils::GetDescriptorPoolSize( m_ReflectionData.ShaderDescriptorSets );
        uint32_t    sets      = m_ReflectionData.ShaderDescriptorSets.size();

        result.Pool.resize( framesInFlight );
        result.DescriptorSets.resize( framesInFlight );

        for ( uint32_t frame = 0; frame < framesInFlight; frame++ )
        {
            VkDescriptorPoolCreateInfo poolInfo = {};
            poolInfo.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount              = poolSizes.size();
            poolInfo.pPoolSizes                 = poolSizes.data();
            poolInfo.maxSets                    = sets;

            VK_CHECK_RESULT( vkCreateDescriptorPool( device, &poolInfo, nullptr, &result.Pool[frame] ) );

            result.DescriptorSets[frame].resize( sets );
        }

        for ( uint32_t frame = 0; frame < framesInFlight; frame++ )
        {
            VkDescriptorSetAllocateInfo allocInfo = {};
            allocInfo.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool              = result.Pool[frame];
            allocInfo.descriptorSetCount          = sets;
            allocInfo.pSetLayouts                 = m_DescriptorSetLayouts.data();

            const VkResult sdf =
                 vkAllocateDescriptorSets( device, &allocInfo, result.DescriptorSets[frame].data() );
            if ( sdf != VK_SUCCESS )
            {
                auto sdfStr = VkResultToString( sdf );
                LOG_TRACE( "\n\n{}\n\n", sdfStr );
            }
        }

        return result;
    }

    void VulkanShader::CreateDescriptorSets( uint32_t framesInFlight )
    {
        m_DescriptorSetInfo = AllocateDescriptorSets( framesInFlight );

        auto& shaderDescriptorSets = m_ReflectionData.ShaderDescriptorSets;

        for ( uint32_t frame = 0; frame < framesInFlight; frame++ )
        {
            for ( SetPoint set = 0; set < shaderDescriptorSets.size(); set++ )
            {
                auto& shaderDescriptorSet = shaderDescriptorSets[set];

                for ( auto& [binding, uniformBuffer] : shaderDescriptorSet.UniformBuffers )
                {
                    VkWriteDescriptorSet& writeDescriptor = uniformBuffer.second.emplace_back();

                    writeDescriptor.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writeDescriptor.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    writeDescriptor.dstSet          = m_DescriptorSetInfo.DescriptorSets[(uint32_t)frame][set];
                    writeDescriptor.dstBinding      = uniformBuffer.first.BindingPoint;
                    writeDescriptor.descriptorCount = 1;
                }

                for ( auto& [binding, sampler] : shaderDescriptorSet.ImageSamplers )
                {
                    VkWriteDescriptorSet& writeDescriptor = sampler.second.emplace_back();

                    writeDescriptor.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writeDescriptor.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writeDescriptor.dstSet          = m_DescriptorSetInfo.DescriptorSets[(uint32_t)frame][set];
                    writeDescriptor.descriptorCount = 1;
                    writeDescriptor.dstBinding      = sampler.first.BindingPoint;
                }
            }
        }
    }

    const VkWriteDescriptorSet VulkanShader::GetWriteDescriptorSet( const WriteDescriptorType& type,
                                                                    uint32_t binding, uint32_t set,
                                                                    uint32_t frame ) const
    {
        switch ( type )
        {
            case WriteDescriptorType::Uniform:
                return m_ReflectionData.ShaderDescriptorSets.at( set ).UniformBuffers.at( binding ).second.at(
                     frame );
        }
    }

} // namespace Desert::Graphic::API::Vulkan