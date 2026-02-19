#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

#include <Engine/ShaderResources/ShaderReflectionTypes.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>
#include <Engine/Core/ShaderCompiler/Includer/ShaderIncluder.hpp>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <ranges>

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
        static Core::Formats::ShaderValueType SPIRTypeToShaderValueType( spirv_cross::SPIRType type )
        {
            switch ( type.basetype )
            {
                case spirv_cross::SPIRType::Boolean:
                    return Core::Formats::ShaderValueType::Bool;
                case spirv_cross::SPIRType::Int:
                    if ( type.vecsize == 1 )
                        return Core::Formats::ShaderValueType::Int;
                    if ( type.vecsize == 2 )
                        return Core::Formats::ShaderValueType::Int2;
                    if ( type.vecsize == 3 )
                        return Core::Formats::ShaderValueType::Int3;
                    if ( type.vecsize == 4 )
                        return Core::Formats::ShaderValueType::Int4;

                case spirv_cross::SPIRType::UInt:
                    return Core::Formats::ShaderValueType::UInt;
                case spirv_cross::SPIRType::Float:
                    if ( type.columns == 3 )
                        return Core::Formats::ShaderValueType::Mat3;
                    if ( type.columns == 4 )
                        return Core::Formats::ShaderValueType::Mat4;

                    if ( type.vecsize == 1 )
                        return Core::Formats::ShaderValueType::Float;
                    if ( type.vecsize == 2 )
                        return Core::Formats::ShaderValueType::Float2;
                    if ( type.vecsize == 3 )
                        return Core::Formats::ShaderValueType::Float3;
                    if ( type.vecsize == 4 )
                        return Core::Formats::ShaderValueType::Float4;
                    break;

                case spirv_cross::SPIRType::Struct:
                    return Core::Formats::ShaderValueType::Struct;
            }
            DESERT_VERIFY( false, "Unknown type!" );
            return Core::Formats::ShaderValueType::Unknown;
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

        Common::ResultStr<std::vector<uint32_t>> Compile( const Core::Formats::ShaderStage stage,
                                                          const std::string                shaderSource,
                                                          const std::string&               shaderPath )
        {
            static shaderc::Compiler compiler;
            shaderc::CompileOptions  shaderCOptions;
            shaderCOptions.SetIncluder( std::make_unique<Core::ShaderIncluder>( shaderPath ) );

            shaderCOptions.SetTargetEnvironment( shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1 );
            shaderCOptions.SetWarningsAsErrors();

            shaderCOptions.SetGenerateDebugInfo();

            // Compile shader
            const shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
                 shaderSource, ShaderStageToShaderC( stage ), shaderPath.c_str(), shaderCOptions );

            if ( module.GetCompilationStatus() != shaderc_compilation_status_success )
            {
                const auto errorMsg =
                     std::format( "{}While compiling shader file: {} \nAt stage: {}", module.GetErrorMessage(),
                                  shaderPath, Shader::GetStringShaderStage( stage ) );

                LOG_ERROR( errorMsg );
                return Common::MakeError<std::vector<uint32_t>>( errorMsg );
            }

            return Common::MakeSuccess( std::vector<uint32_t>( module.begin(), module.end() ) );
        }

        Common::BoolResultStr CreateShaderModule( const Core::Formats::ShaderStage&             stage,
                                                  const std::vector<uint32_t>&                  shaderData,
                                                  const std::filesystem::path&                  filepath,
                                                  std::vector<VkPipelineShaderStageCreateInfo>& writeCreateInfo )
        {
            VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                   ->GetVulkanLogicalDevice();

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

        bool IsProgramFile( const std::string& source )
        {
            return source.find( "#pragma program" ) != std::string::npos;
        }

        std::string ExtractProgramName( const std::string& source )
        {
            std::istringstream stream( source );
            std::string        line;

            while ( std::getline( stream, line ) )
            {
                if ( line.find( "#pragma program" ) == std::string::npos )
                    continue;

                std::istringstream ls( line );
                std::string        pragma, programKeyword, name;
                ls >> pragma >> programKeyword >> name;

                return name;
            }

            DESERT_VERIFY( false, "Shader program has no '#pragma program <name>'" );
            return {};
        }
    } // namespace Utils

    VulkanShader::VulkanShader( const Assets::Asset<Assets::ShaderAsset>& asset, const ShaderDefines& defines )
         : m_ShaderAsset( asset ), m_ShaderPath( asset->GetMetadata().Filepath ), m_ShaderName()
    {
        Utils::CreateDirectoriesIfNoExists(); // TODO: move to a better location (init project)

        Reload();
    }

    Common::BoolResultStr VulkanShader::Reload() // also load
    {
        const auto asset = m_ShaderAsset.lock();
        if ( !asset )
        {
            DESERT_VERIFY( false );
        }
        m_PipelineShaderStageCreateInfos.clear();
        m_ReflectionData.ShaderDescriptorSets.clear();

        const auto shaderContent = asset->GetShaderContent();

        m_ShaderName = Utils::ExtractProgramName( shaderContent ); // bad way to get a shader name

        if ( Utils::IsProgramFile( shaderContent ) )
        {
            auto program = Core::Preprocess::ShaderPreprocess::PreProcessProgram( shaderContent, m_ShaderPath );
            return CompileProgram( program );
        }

        DESERT_VERIFY( false );
        return Common::MakeError( "" );
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
                    ShaderResources::ShaderLayout::UniformBuffer uniformBuffer;
                    uniformBuffer.BindingPoint = binding;
                    uniformBuffer.Size         = size;
                    uniformBuffer.Name         = name;
                    uniformBuffer.ShaderStage =
                         Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                    for ( uint32_t i = 0; i < memberCount; i++ )
                    {
                        uint32_t                     member_type_id = bufferType.member_types[i];
                        const spirv_cross::SPIRType& member_type    = compiler.get_type( member_type_id );
                        const std::string&           member_name = compiler.get_member_name( bufferType.self, i );

                        Desert::ShaderResources::ShaderLayout::ShaderFieldLayout field;
                        field.Name          = member_name;
                        field.FieldDataType = Utils::SPIRTypeToShaderValueType( member_type );
                        field.Size          = compiler.get_declared_struct_member_size( bufferType, i );
                        field.Offset        = compiler.type_struct_member_offset( bufferType, i );

                        if ( member_type.array.size() > 0 )
                        {
                            field.ArraySize = member_type.array[0];
                            // for ( auto dim : member_type.array ) { ... }
                        }
                        else
                        {
                            field.ArraySize = 1;
                        }

                        uniformBuffer.Fields.push_back( field );
                    }

                    uniformBuffers.insert( { binding, uniformBuffer } );
                }
                else
                {
                    auto& existingBuffer = uniformBuffers[binding];

                    if ( existingBuffer.Fields.size() != memberCount )
                    {
                        existingBuffer.Fields.clear();

                        for ( uint32_t i = 0; i < memberCount; i++ )
                        {
                            uint32_t                     member_type_id = bufferType.member_types[i];
                            const spirv_cross::SPIRType& member_type    = compiler.get_type( member_type_id );
                            const std::string& member_name = compiler.get_member_name( bufferType.self, i );

                            Desert::ShaderResources::ShaderLayout::ShaderFieldLayout field;
                            field.Name          = member_name;
                            field.FieldDataType = Utils::SPIRTypeToShaderValueType( member_type );
                            field.Size          = compiler.get_declared_struct_member_size( bufferType, i );
                            field.Offset        = compiler.type_struct_member_offset( bufferType, i );

                            if ( member_type.array.size() > 0 )
                            {
                                field.ArraySize = member_type.array[0];
                            }
                            else
                            {
                                field.ArraySize = 1;
                            }

                            existingBuffer.Fields.push_back( field );
                        }
                    }
                }

                LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
                LOG_TRACE( "  Member Count: {0}", memberCount );
                LOG_TRACE( "  Size: {0}", size );

                for ( uint32_t i = 0; i < memberCount; i++ )
                {
                    uint32_t                     member_type_id = bufferType.member_types[i];
                    const spirv_cross::SPIRType& member_type    = compiler.get_type( member_type_id );
                    const std::string&           member_name    = compiler.get_member_name( bufferType.self, i );
                    uint32_t member_size   = compiler.get_declared_struct_member_size( bufferType, i );
                    uint32_t member_offset = compiler.type_struct_member_offset( bufferType, i );

                    std::string arrayInfo = "";
                    if ( member_type.array.size() > 0 )
                    {
                        arrayInfo = ", ArraySize: " + std::to_string( member_type.array[0] );
                    }

                    LOG_TRACE( "    Field {0}: {1} (Type: {2}, Size: {3}, Offset: {4}{5})", i, member_name, "TODO",
                               member_size, member_offset, arrayInfo );
                }

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

            bool isCube = false;
            if ( imageType.image.dim == spv::DimCube )
            {
                isCube = true;
            }

            // TODO: more readable
            if ( !isCube )
            {
                auto& imageSampler = m_ReflectionData.ShaderDescriptorSets[descriptorSet].Image2DSamplers;

                if ( imageSampler.find( binding ) == imageSampler.end() )
                {
                    uint32_t arraySize = 1;
                    if ( Utils::IsArray( imageType ) )
                    {
                        arraySize = Utils::GetArraySize( imageType );
                    }

                    ShaderResources::ShaderLayout::Image2DSampler newImageSampler;
                    newImageSampler.BindingPoint = binding;
                    newImageSampler.Name         = name;
                    newImageSampler.ArraySize    = arraySize;
                    newImageSampler.ShaderStage =
                         Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                    imageSampler.insert( { binding, newImageSampler } );
                }

                LOG_TRACE( "  {0} (type: 2D) ({1}, {2})", name, descriptorSet, binding );
            }

            else
            {
                auto& imageSampler = m_ReflectionData.ShaderDescriptorSets[descriptorSet].ImageCubeSamplers;

                if ( imageSampler.find( binding ) == imageSampler.end() )
                {
                    uint32_t arraySize = 1;
                    if ( Utils::IsArray( imageType ) )
                    {
                        arraySize = Utils::GetArraySize( imageType );
                    }

                    ShaderResources::ShaderLayout::ImageCubeSampler newImageSampler;
                    newImageSampler.BindingPoint = binding;
                    newImageSampler.Name         = name;
                    newImageSampler.ArraySize    = arraySize;
                    newImageSampler.ShaderStage =
                         Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                    imageSampler.insert( { binding, newImageSampler } );
                }

                LOG_TRACE( "  {0} (type: Cube) ({1}, {2})", name, descriptorSet, binding );
            }
        }

        LOG_TRACE( "Storage Buffers: " );
        for ( const auto& resource : resources.storage_buffers )
        {
            const auto& name          = resource.name;
            uint32_t    binding       = compiler.get_decoration( resource.id, spv::DecorationBinding );
            uint32_t    descriptorSet = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            auto&       bufferType    = compiler.get_type( resource.base_type_id );
            uint32_t    size          = (uint32_t)compiler.get_declared_struct_size( bufferType );

            auto& storageBuffers = m_ReflectionData.ShaderDescriptorSets[descriptorSet].StorageBuffers;

            if ( storageBuffers.find( binding ) == storageBuffers.end() )
            {
                ShaderResources::ShaderLayout::StorageBuffer storageBuffer;
                storageBuffer.BindingPoint = binding;
                storageBuffer.Name         = name;
                storageBuffer.Size         = size;
                storageBuffer.ShaderStage = Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                storageBuffers.insert( { binding, storageBuffer } );
            }

            LOG_TRACE( "  {0} ({1}, {2})", name, descriptorSet, binding );
        }

        LOG_TRACE( "PushConstant Buffers: " );
        for ( const auto& resource : resources.push_constant_buffers )
        {
            auto activeBuffers = compiler.get_active_buffer_ranges( resource.id );
            if ( activeBuffers.size() )
            {
                const auto& name        = resource.name;
                auto&       bufferType  = compiler.get_type( resource.base_type_id );
                int         memberCount = (uint32_t)bufferType.member_types.size();
                uint32_t    size        = (uint32_t)compiler.get_declared_struct_size( bufferType );

                auto& pushConstantReflection = m_ReflectionData.PushConstantRanges;

                if ( !pushConstantReflection )
                {
                    ShaderResources::ShaderLayout::PushConstantRange pushConstant;
                    pushConstant.Size   = size;
                    pushConstant.Offset = 0;
                    pushConstant.Name   = name;
                    pushConstant.ShaderStage =
                         Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                    pushConstantReflection = pushConstant;
                }
                else
                {
                }

                LOG_TRACE( "  {0}", name );
                LOG_TRACE( "  Member Count: {0}", memberCount );
                LOG_TRACE( "  Size: {0}", size );
                LOG_TRACE( "-------------------" );
            }
        }

        LOG_TRACE( "Storage Images: " );
        for ( const auto& resource : resources.storage_images )
        {
            const auto& name          = resource.name;
            uint32_t    binding       = compiler.get_decoration( resource.id, spv::DecorationBinding );
            uint32_t    descriptorSet = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            auto&       imageType     = compiler.get_type( resource.base_type_id );

            auto& imageSampler = m_ReflectionData.ShaderDescriptorSets[descriptorSet].StorageImage2DSamplers;

            if ( imageSampler.find( binding ) == imageSampler.end() )
            {
                uint32_t arraySize = 1;
                if ( Utils::IsArray( imageType ) )
                {
                    arraySize = Utils::GetArraySize( imageType );
                }

                ShaderResources::ShaderLayout::Image2DSampler newImageSampler;
                newImageSampler.BindingPoint = binding;
                newImageSampler.Name         = name;
                newImageSampler.ArraySize    = arraySize;
                newImageSampler.ShaderStage =
                     Utils::GetShaderStageFlagBitsFromSPV( compiler.get_execution_model() );

                imageSampler.insert( { binding, newImageSampler } );
            }
        }
    }

    Common::BoolResultStr VulkanShader::CreateDescriptorsLayout()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();

        if ( m_ReflectionData.ShaderDescriptorSets.empty() )
        {
            return Common::MakeSuccess( true );
        }

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
                layout.stageFlags         = Utils::ShaderStageToVkShader( uniformBuffer.ShaderStage );
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1; // not array at now
            }

            // Samplers 2D
            for ( const auto& [binding, sampler] : shaderDescriptorSet.Image2DSamplers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                layout.stageFlags         = Utils::ShaderStageToVkShader( sampler.ShaderStage );
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1;
            }

            // Samplers Cube
            for ( const auto& [binding, sampler] : shaderDescriptorSet.ImageCubeSamplers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                layout.stageFlags         = Utils::ShaderStageToVkShader( sampler.ShaderStage );
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1;
            }

            // Storage Samplers
            for ( const auto& [binding, sampler] : shaderDescriptorSet.StorageImage2DSamplers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                layout.stageFlags         = Utils::ShaderStageToVkShader( sampler.ShaderStage );
                layout.pImmutableSamplers = nullptr;
                layout.descriptorCount    = 1;
            }

            // Storage buffers
            for ( const auto& [binding, storageBuffer] : shaderDescriptorSet.StorageBuffers )
            {
                auto& layout              = layoutBindings.emplace_back();
                layout.binding            = binding;
                layout.descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                layout.stageFlags         = Utils::ShaderStageToVkShader( storageBuffer.ShaderStage );
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

    const std::vector<Desert::ShaderResources::ShaderLayout::UniformBuffer>
    VulkanShader::GetUniformBufferModels() const
    {
        if ( !m_ReflectionData.ShaderDescriptorSets.size() ) [[unlikely]]
        {
            return {};
        }
        else [[likely]]
        {
            const auto& uniformInfo = m_ReflectionData.ShaderDescriptorSets.at( 0 ).UniformBuffers;
            auto        res =
                 uniformInfo | std::views::values | std::views::transform( []( const auto& p ) { return p; } );
            return { res.begin(), res.end() };
        }
    }

    const std::vector<Desert::ShaderResources::ShaderLayout::ImageCubeSampler>
    VulkanShader::GetUniformImageCubeModels() const
    {
        if ( !m_ReflectionData.ShaderDescriptorSets.size() ) [[unlikely]]
        {
            return {};
        }
        else [[likely]]
        {
            const auto& uniformInfo = m_ReflectionData.ShaderDescriptorSets.at( 0 ).ImageCubeSamplers;
            auto        res =
                 uniformInfo | std::views::values | std::views::transform( []( const auto& p ) { return p; } );
            return { res.begin(), res.end() };
        }
    }

    const std::vector<Desert::ShaderResources::ShaderLayout::Image2DSampler>
    VulkanShader::GetUniformImage2DModels() const
    {
        if ( !m_ReflectionData.ShaderDescriptorSets.size() ) [[unlikely]]
        {
            return {};
        }
        else [[likely]]
        {
            const auto& uniformInfo = m_ReflectionData.ShaderDescriptorSets.at( 0 ).Image2DSamplers;
            auto        res =
                 uniformInfo | std::views::values | std::views::transform( []( const auto& p ) { return p; } );
            return { res.begin(), res.end() };
        }
    }

    const std::vector<ShaderResources::ShaderLayout::StorageBuffer> VulkanShader::GetStorageBufferModels() const
    {
        if ( !m_ReflectionData.ShaderDescriptorSets.size() ) [[unlikely]]
        {
            return {};
        }
        else [[likely]]
        {
            const auto& uniformInfo = m_ReflectionData.ShaderDescriptorSets.at( 0 ).StorageBuffers;
            auto        res =
                 uniformInfo | std::views::values | std::views::transform( []( const auto& p ) { return p; } );
            return { res.begin(), res.end() };
        }
    }

    Common::BoolResultStr
    VulkanShader::CompileProgram( const std::unordered_map<Core::Formats::ShaderStage, std::string>& stages )
    {
        LOG_INFO( "Compiling shader program: {}", m_ShaderPath.filename().string() );

        for ( const auto& [stage, source] : stages )
        {
            LOG_TRACE( "  Stage: {}", Shader::GetStringShaderStage( stage ) );

            const auto compileResult = Utils::Compile( stage, source, m_ShaderPath.string() );

            if ( !compileResult.IsSuccess() )
            {
                LOG_ERROR( "  Failed to compile {} stage", Shader::GetStringShaderStage( stage ) );
                return Common::MakeError<bool>( compileResult.GetError() );
            }

            LOG_TRACE( "  {} stage compiled successfully", Shader::GetStringShaderStage( stage ) );

            const auto createShaderModuleResult = Utils::CreateShaderModule(
                 stage, compileResult.GetValue(), m_ShaderPath, m_PipelineShaderStageCreateInfos );

            if ( !createShaderModuleResult.IsSuccess() )
            {
                LOG_ERROR( "  Failed to create shader module for {} stage",
                           Shader::GetStringShaderStage( stage ) );
                return Common::MakeError<bool>( createShaderModuleResult.GetError() );
            }

            Reflect( Utils::ShaderStageToVkShader( stage ), compileResult.GetValue() );
        }

        const auto createDescriptorsResult = CreateDescriptorsLayout();
        if ( !createDescriptorsResult.IsSuccess() )
        {
            LOG_ERROR( "Failed to create descriptor layouts for shader program" );
            return Common::MakeError<bool>( createDescriptorsResult.GetError() );
        }

        LOG_INFO( "Shader program {} compiled successfully", m_ShaderPath.filename().string() );

        return BOOLSUCCESS;
    }

} // namespace Desert::Graphic::API::Vulkan