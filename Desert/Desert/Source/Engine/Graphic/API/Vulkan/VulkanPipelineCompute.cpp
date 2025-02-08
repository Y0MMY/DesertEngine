#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanPipelineCompute::VulkanPipelineCompute( const std::shared_ptr<Shader> shader ) : m_Shader( shader )
    {
    }

    void VulkanPipelineCompute::Begin()
    {
        uint32_t frameIndex          = Renderer::GetInstance().GetCurrentFrameIndex();
        m_ActiveComputeCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer( frameIndex );
    }

    void VulkanPipelineCompute::End()
    {
    }

    void VulkanPipelineCompute::Invalidate()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        const auto vulkanShader         = std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Shader );
        auto       descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
        pipelineLayoutCreateInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
        pipelineLayoutCreateInfo.pSetLayouts    = descriptorSetLayouts.data();

        VK_CHECK_RESULT(
             vkCreatePipelineLayout( device, &pipelineLayoutCreateInfo, nullptr, &m_ComputePipelineLayout ) );

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage  = vulkanShader->GetPipelineShaderStageCreateInfos()[0];
        pipelineInfo.layout = m_ComputePipelineLayout;

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType                     = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

        VK_CHECK_RESULT( vkCreatePipelineCache( device, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache ) );
        VK_CHECK_RESULT(
             vkCreateComputePipelines( device, m_PipelineCache, 1, &pipelineInfo, nullptr, &m_ComputePipeline ) );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_PIPELINE, m_Shader->GetName(),
                                          m_ComputePipeline );
    }

    void VulkanPipelineCompute::Execute( VkDescriptorSet descriptorSet )
    {
        vkCmdBindPipeline( m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline );
        vkCmdBindDescriptorSets( m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 m_ComputePipelineLayout, 0, 1, &descriptorSet, 0, nullptr );
    }

} // namespace Desert::Graphic::API::Vulkan