#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShaderReflection.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Core/ShaderCompiler/ShaderCompiler.hpp>
#include <Engine/Core/ShaderCompiler/ShaderPreprocess/ShaderPreprocessor.hpp>

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

            auto reflectResult = Reflect( vkStage, spirv );
            if ( !reflectResult.IsSuccess() )
                return Common::MakeError( reflectResult.GetError() );
        }

        return CreateDescriptorsLayout();
    }

    Common::BoolResultStr VulkanShader::Reflect( VkShaderStageFlagBits        vkStage,
                                                 const std::vector<uint32_t>& spirv )
    {
        const Core::Formats::ShaderStage stage = ReflectionUtils::VkStageToStage( vkStage );

        // The reflection proper lives in a device-free translation unit: it is the part that decides
        // which descriptor bucket every resource lands in, and that decision used to be made from the
        // variable's NAME. Keeping it free of VkDevice is what lets a test prove a sampler3D is not
        // filed as a 2D sampler (Desert/Tests/Engine/ShaderReflection).
        const auto diagnostics = ShaderReflection::ReflectStage( spirv, stage, m_ReflectionData );
        if ( diagnostics.empty() )
        {
            return BOOLSUCCESS;
        }

        // Every refused resource is named, with its real type, before the shader is dropped: a missing
        // binding surfaces far from its cause, and a silently mis-filed one never surfaces at all.
        for ( const auto& message : diagnostics )
        {
            LOG_ERROR( "Shader '{}' [{}]: {}", m_ShaderName, GetStringShaderStage( stage ), message );
        }

        return Common::MakeFormattedError( "Shader '{}' [{}]: {} unsupported resource(s); first: {}", m_ShaderName,
                                           GetStringShaderStage( stage ), diagnostics.size(),
                                           diagnostics.front() );
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
            for ( const auto& [b, res] : descriptorSet.Image3DSamplers )
                Add( b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.ImageCubeSamplers ) Add( b, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.StorageBuffers ) Add( b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.StorageImage2DSamplers ) Add( b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, res.ShaderStage );
            for ( const auto& [b, res] : descriptorSet.StorageImage3DSamplers )
                Add( b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, res.ShaderStage );

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
