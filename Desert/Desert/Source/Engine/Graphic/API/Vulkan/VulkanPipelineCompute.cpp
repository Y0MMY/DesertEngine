#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Core/EngineContext.hpp>

#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanPipelineCompute::VulkanPipelineCompute( const ComputePipelineSpecification& spec ) 
        : m_Specification( spec )
    {
        m_VulkanMaterialBackend = std::make_unique<VulkanMaterialBackend>( spec.Shader );
    }

    VulkanPipelineCompute::~VulkanPipelineCompute()
    {
        Release();
    }

    void VulkanPipelineCompute::UpdateStorageBuffer( void* data, std::size_t size )
    {
        DESERT_VERIFY( size <= 128 );
        m_StorageBuffer = Common::Memory::Buffer::Copy( data, size );
    }

    void VulkanPipelineCompute::Invalidate()
    {
        Release();

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                              ->GetVulkanLogicalDevice();
        const auto vulkanShader         = sp_cast<VulkanShader>( m_Specification.Shader );
        auto       descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
        pipelineLayoutCreateInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>( descriptorSetLayouts.size() );
        pipelineLayoutCreateInfo.pSetLayouts    = descriptorSetLayouts.data();

        const auto& pushConstantRange = vulkanShader->GetShaderPushConstant();
        if ( pushConstantRange )
        {
            VkPushConstantRange vulkanPushConstantRange{ .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                                         .offset     = pushConstantRange->Offset,
                                                         .size       = pushConstantRange->Size };

            pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
            pipelineLayoutCreateInfo.pPushConstantRanges    = &vulkanPushConstantRange;
        }

        VK_CHECK_RESULT( vkCreatePipelineLayout( device, &pipelineLayoutCreateInfo, nullptr, &m_ComputePipelineLayout ) );

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        VK_CHECK_RESULT( vkCreatePipelineCache( device, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache ) );

        VkComputePipelineCreateInfo pipelineInfo{ .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                  .stage  = vulkanShader->GetPipelineShaderStageCreateInfos()[0],
                                                  .layout = m_ComputePipelineLayout };

        VK_CHECK_RESULT( vkCreateComputePipelines( device, m_PipelineCache, 1, &pipelineInfo, nullptr, &m_ComputePipeline ) );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_PIPELINE, m_Specification.Shader->GetName(), m_ComputePipeline );

        m_VulkanMaterialBackend->InitializeDefaults();
    }

    void VulkanPipelineCompute::Release()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                              ->GetVulkanLogicalDevice();
        if ( m_ComputePipeline != VK_NULL_HANDLE )
        {
            vkDestroyPipeline( device, m_ComputePipeline, nullptr );
            m_ComputePipeline = VK_NULL_HANDLE;
        }

        if ( m_ComputePipelineLayout != VK_NULL_HANDLE )
        {
            vkDestroyPipelineLayout( device, m_ComputePipelineLayout, nullptr );
            m_ComputePipelineLayout = VK_NULL_HANDLE;
        }

        if ( m_PipelineCache != VK_NULL_HANDLE )
        {
            vkDestroyPipelineCache( device, m_PipelineCache, nullptr );
            m_PipelineCache = VK_NULL_HANDLE;
        }

        m_ActiveComputeCommandBuffer = VK_NULL_HANDLE;
    }

    void VulkanPipelineCompute::BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex )
    {
        // This will be used by the executor
        m_VulkanMaterialBackend->BindDescriptorSets( m_ActiveComputeCommandBuffer, m_ComputePipelineLayout,
                                                     VK_PIPELINE_BIND_POINT_COMPUTE, frameIndex );
    }

    void VulkanPipelineCompute::UpdateDescriptorSet( uint32_t                                 frameIndex,
                                                     const std::vector<VkWriteDescriptorSet>& writes,
                                                     VkDescriptorSet descriptorSet, uint32_t setIndex /*= 0 */ )
    {
        std::vector<VkWriteDescriptorSet> modifiedWrites = writes;
        for ( auto& write : modifiedWrites )
        {
            write.dstSet = descriptorSet;
        }
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                              ->GetVulkanLogicalDevice();        vkUpdateDescriptorSets( device, static_cast<uint32_t>( modifiedWrites.size() ), modifiedWrites.data(), 0,
                                nullptr );
    }

} // namespace Desert::Graphic::API::Vulkan
