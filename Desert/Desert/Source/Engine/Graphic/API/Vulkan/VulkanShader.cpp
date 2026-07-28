#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Core/ShaderCompiler/ShaderCompiler.hpp>
#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

#include <spirv_cross/spirv_glsl.hpp>

namespace Desert::Graphic::API::Vulkan
{
    namespace ReflectionUtils
    {
        static VkShaderStageFlags StageToVkStage( Core::Formats::ShaderStage stage )
        {
            return (VkShaderStageFlags)stage;
        }
        
        static Core::Formats::ShaderStage VkStageToStage( VkShaderStageFlagBits stage )
        {
            return (Core::Formats::ShaderStage)stage;
        }
    }

    VulkanShader::VulkanShader( const Assets::Asset<Assets::ShaderAsset>& asset, const ShaderDefines& defines,
                                const std::string& passName )
        : m_ShaderAsset( asset ), m_PassName( passName )
    {
        m_ShaderPath = asset->GetMetadata().Filepath;
        m_ShaderName = m_ShaderPath.stem().string();
        if ( !m_PassName.empty() )
            m_ShaderName += "/" + m_PassName;

        Reload();
    }

    VulkanShader::~VulkanShader()
    {
        auto device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();
        for ( auto module : m_ShaderModules ) vkDestroyShaderModule( device, module, nullptr );
        for ( auto layout : m_DescriptorSetLayouts ) vkDestroyDescriptorSetLayout( device, layout, nullptr );
    }

    Common::BoolResultStr VulkanShader::Reload()
    {
        auto asset = m_ShaderAsset.lock();
        if ( !asset ) return Common::MakeError( "Shader asset expired" );

        m_ProgramMeta =
             Core::Preprocess::ShaderPreprocess::ParseProgramMetaForPass( asset->GetShaderContent(), m_PassName );
        if ( m_ProgramMeta.HasParams() || m_ProgramMeta.State.Topology.has_value() )
        {
            LOG_INFO( "Shader '{}': parsed {} param(s) + render-state from shader metadata", m_ShaderName,
                      m_ProgramMeta.Params.size() );
        }

        auto stages = Core::Preprocess::ShaderPreprocess::PreProcessProgramPass( asset->GetShaderContent(),
                                                                                 m_ShaderPath, m_PassName );
        return CompileProgram( stages );
    }

    Common::BoolResultStr VulkanShader::CompileProgram( const std::unordered_map<Core::Formats::ShaderStage, std::string>& stages )
    {
        auto device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();

        // Cleanup old
        for ( auto module : m_ShaderModules ) vkDestroyShaderModule( device, module, nullptr );
        for ( auto layout : m_DescriptorSetLayouts ) vkDestroyDescriptorSetLayout( device, layout, nullptr );
        
        m_ShaderModules.clear();
        m_DescriptorSetLayouts.clear();
        m_PipelineShaderStageCreateInfos.clear();
        m_ReflectionData.ShaderDescriptorSets.clear();
        m_ReflectionData.PushConstantRanges = std::nullopt;

        for ( const auto& [stage, source] : stages )
        {
            auto spirvResult = Core::ShaderCompiler::CompileGLSLToSPIRV( stage, source, m_ShaderPath.string() );
            if ( !spirvResult.IsSuccess() ) return Common::MakeError( spirvResult.GetError() );

            const auto& spirv = spirvResult.GetValue();
            VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (uint32_t)(spirv.size() * 4), .pCode = spirv.data() };
            VkShaderModule module;
            VK_CHECK_RESULT_BOOL( vkCreateShaderModule( device, &ci, nullptr, &module ) );
            m_ShaderModules.push_back( module );

            // Debug name so RenderDoc/validation messages identify the module ("Unlit/Shadow [Vertex]").
            VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_SHADER_MODULE,
                                              m_ShaderName + " [" + GetStringShaderStage( stage ) + "]",
                                              module );

            VkShaderStageFlagBits vkStage = (VkShaderStageFlagBits)ReflectionUtils::StageToVkStage( stage );
            m_PipelineShaderStageCreateInfos.push_back( { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = vkStage, .module = module, .pName = "main" } );

            Reflect( vkStage, spirv );
        }

        return CreateDescriptorsLayout();
    }

    void VulkanShader::Reflect( VkShaderStageFlagBits vkStage, const std::vector<uint32_t>& spirv )
    {
        spirv_cross::CompilerGLSL compiler( spirv );
        spirv_cross::ShaderResources resources = compiler.get_shader_resources();
        Core::Formats::ShaderStage stage = ReflectionUtils::VkStageToStage( vkStage );

        // Uniform Buffers
        for ( const auto& resource : resources.uniform_buffers )
        {
            uint32_t set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            auto& ub = m_ReflectionData.ShaderDescriptorSets[set].UniformBuffers[binding];
            ub.BindingPoint = binding; ub.Name = resource.name;
            ub.ShaderStage = (Core::Formats::ShaderStage)((uint32_t)ub.ShaderStage | (uint32_t)stage);

            const auto& structType = compiler.get_type( resource.base_type_id );
            ub.Size = (uint32_t)compiler.get_declared_struct_size( structType );

            // Populate fields once; multi-stage shaders call Reflect() per stage, avoid duplicates.
            if ( ub.Fields.empty() )
            {
                for ( uint32_t i = 0; i < (uint32_t)structType.member_types.size(); ++i )
                {
                    ShaderResources::ShaderLayout::ShaderFieldLayout field;
                    field.Name   = compiler.get_member_name( resource.base_type_id, i );
                    field.Offset = compiler.type_struct_member_offset( structType, i );
                    field.Size   = (uint32_t)compiler.get_declared_struct_member_size( structType, i );

                    const auto& memberType = compiler.get_type( structType.member_types[i] );
                    field.ArraySize = memberType.array.empty() ? 1u : memberType.array[0];

                    ub.Fields.push_back( std::move( field ) );
                }
            }
        }

        // Samplers
        for ( const auto& resource : resources.sampled_images )
        {
            uint32_t set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            if ( resource.name.find( "Env" ) != std::string::npos || resource.name.find( "Cube" ) != std::string::npos ) {
                auto& sc = m_ReflectionData.ShaderDescriptorSets[set].ImageCubeSamplers[binding];
                sc.BindingPoint = binding; sc.Name = resource.name; sc.ShaderStage = (Core::Formats::ShaderStage)((uint32_t)sc.ShaderStage | (uint32_t)stage);
            } else {
                auto& s2 = m_ReflectionData.ShaderDescriptorSets[set].Image2DSamplers[binding];
                s2.BindingPoint = binding; s2.Name = resource.name; s2.ShaderStage = (Core::Formats::ShaderStage)((uint32_t)s2.ShaderStage | (uint32_t)stage);
            }
        }

        // Storage Buffers
        for ( const auto& resource : resources.storage_buffers )
        {
            uint32_t set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            auto& sb = m_ReflectionData.ShaderDescriptorSets[set].StorageBuffers[binding];
            sb.BindingPoint = binding; sb.Name = resource.name; sb.ShaderStage = (Core::Formats::ShaderStage)((uint32_t)sb.ShaderStage | (uint32_t)stage);
            auto& type = compiler.get_type( resource.base_type_id );
            sb.Size = ( type.member_types.empty() || type.array.size() > 0 ) ? 0 : (uint32_t)compiler.get_declared_struct_size( type );
        }

        // Storage Images (e.g. writeonly imageCube / image2D in compute shaders)
        for ( const auto& resource : resources.storage_images )
        {
            uint32_t set     = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
            uint32_t binding = compiler.get_decoration( resource.id, spv::DecorationBinding );
            auto& si         = m_ReflectionData.ShaderDescriptorSets[set].StorageImage2DSamplers[binding];
            si.BindingPoint  = binding;
            si.Name          = resource.name;
            si.ShaderStage   = (Core::Formats::ShaderStage)( (uint32_t)si.ShaderStage | (uint32_t)stage );
        }

        // Push Constants
        if ( !resources.push_constant_buffers.empty() )
        {
            const auto& res = resources.push_constant_buffers[0];
            auto& type = compiler.get_type( res.base_type_id );
            const uint32_t declaredSize = (uint32_t)compiler.get_declared_struct_size( type );
            if ( !m_ReflectionData.PushConstantRanges ) {
                ShaderResources::ShaderLayout::PushConstantRange range;
                range.Offset = 0; range.Size = declaredSize;
                range.Name = res.name; range.ShaderStage = stage;
                m_ReflectionData.PushConstantRanges = range;
            } else {
                m_ReflectionData.PushConstantRanges->ShaderStage = (Core::Formats::ShaderStage)((uint32_t)m_ReflectionData.PushConstantRanges->ShaderStage | (uint32_t)stage);
                // Different stages may declare the same push-constant block but glslang strips members
                // a stage doesn't use, so each reports a different declared size. The pipeline-layout
                // range must span the largest, or a stage's access lands outside the range.
                if ( declaredSize > m_ReflectionData.PushConstantRanges->Size )
                    m_ReflectionData.PushConstantRanges->Size = declaredSize;
            }
        }
    }

    Common::BoolResultStr VulkanShader::CreateDescriptorsLayout()
    {
        auto device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )->GetVulkanLogicalDevice();
        for ( const auto& [setIndex, descriptorSet] : m_ReflectionData.ShaderDescriptorSets )
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;
            auto Add = [&]( uint32_t b, VkDescriptorType t, Core::Formats::ShaderStage s ) {
                bindings.push_back( { .binding = b, .descriptorType = t, .descriptorCount = 1, .stageFlags = (VkShaderStageFlags)s } );
            };
            for ( const auto& [b, res] : descriptorSet.UniformBuffers ) Add( b, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.Image2DSamplers ) Add( b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.ImageCubeSamplers ) Add( b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.StorageBuffers ) Add( b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.StorageImage2DSamplers ) Add( b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, res.ShaderStage );

            VkDescriptorSetLayoutCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = (uint32_t)bindings.size(), .pBindings = bindings.data() };
            if ( setIndex >= m_DescriptorSetLayouts.size() ) m_DescriptorSetLayouts.resize( setIndex + 1 );
            VK_CHECK_RESULT_BOOL( vkCreateDescriptorSetLayout( device, &ci, nullptr, &m_DescriptorSetLayouts[setIndex] ) );
        }
        return BOOLSUCCESS;
    }

    const std::vector<ShaderResources::ShaderLayout::UniformBuffer> VulkanShader::GetUniformBufferModels() const 
    { 
        std::vector<ShaderResources::ShaderLayout::UniformBuffer> res;
        for ( const auto& [set, dSet] : m_ReflectionData.ShaderDescriptorSets )
            for ( const auto& [b, ub] : dSet.UniformBuffers ) res.push_back( ub );
        return res;
    }

    const std::vector<ShaderResources::ShaderLayout::StorageBuffer> VulkanShader::GetStorageBufferModels() const 
    { 
        std::vector<ShaderResources::ShaderLayout::StorageBuffer> res;
        for ( const auto& [set, dSet] : m_ReflectionData.ShaderDescriptorSets )
            for ( const auto& [b, sb] : dSet.StorageBuffers ) res.push_back( sb );
        return res;
    }

    const std::vector<ShaderResources::ShaderLayout::ImageCubeSampler> VulkanShader::GetUniformImageCubeModels() const 
    { 
        std::vector<ShaderResources::ShaderLayout::ImageCubeSampler> res;
        for ( const auto& [set, dSet] : m_ReflectionData.ShaderDescriptorSets )
            for ( const auto& [b, sc] : dSet.ImageCubeSamplers ) res.push_back( sc );
        return res;
    }

    const std::vector<ShaderResources::ShaderLayout::Image2DSampler> VulkanShader::GetUniformImage2DModels() const 
    { 
        std::vector<ShaderResources::ShaderLayout::Image2DSampler> res;
        for ( const auto& [set, dSet] : m_ReflectionData.ShaderDescriptorSets )
            for ( const auto& [b, s2] : dSet.Image2DSamplers ) res.push_back( s2 );
        return res;
    }

} // namespace Desert::Graphic::API::Vulkan
