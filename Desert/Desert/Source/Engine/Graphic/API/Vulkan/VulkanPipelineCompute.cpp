#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanPipelineCompute::VulkanPipelineCompute( const std::shared_ptr<Shader>& shader ) : m_Shader( shader )
    {
    }

    void VulkanPipelineCompute::Begin()
    {
        m_ActiveComputeCommandBuffer = VulkanRenderCommandBuffer::GetInstance().GetCommandBuffer( true );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer( m_ActiveComputeCommandBuffer, &beginInfo );
        vkCmdBindPipeline( m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline );
    }

    void VulkanPipelineCompute::End()
    {
         
        vkEndCommandBuffer( m_ActiveComputeCommandBuffer );

        VkSubmitInfo submitInfo       = {};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &m_ActiveComputeCommandBuffer;

        VkFence fence;
        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags             = 0;
        const auto& device                = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();
        vkCreateFence( device, &fenceCreateInfo, nullptr, &fence );
        VkQueue computeQueue = VulkanLogicalDevice::GetInstance().GetComputeQueue();
        vkQueueSubmit( computeQueue, 1, &submitInfo, fence );

        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK_RESULT( vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX ) );
        vkDestroyFence( device, fence, nullptr );

        m_ActiveComputeCommandBuffer = VK_NULL_HANDLE;
    }

    void VulkanPipelineCompute::Execute( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )
    {
        Begin();
        Dispatch( groupCountX, groupCountY, groupCountZ );
        End();
    }

    void VulkanPipelineCompute::Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )
    {
        vkCmdDispatch( m_ActiveComputeCommandBuffer, groupCountX, groupCountY, groupCountZ );
    }

    void VulkanPipelineCompute::Invalidate()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

        const auto vulkanShader         = std::static_pointer_cast<Graphic::API::Vulkan::VulkanShader>( m_Shader );
        auto       descriptorSetLayouts = vulkanShader->GetAllDescriptorSetLayouts();

        const auto descriptorSets = vulkanShader->GetShaderDescriptorSets();
        if ( !descriptorSets.empty() )
        {
            vulkanShader->CreateDescriptorSets( 3 ); // TODO: frame in flight
        }

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

    void VulkanPipelineCompute::ReadBuffer( uint32_t bufferSize )
    {
        /* auto& allocator = VulkanAllocator::GetInstance();

         VkBufferCreateInfo bufferCreateInfo;
         bufferCreateInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
         bufferCreateInfo.size        = bufferSize;
         bufferCreateInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
         bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;


         VkBuffer buffer;
         const auto buffer = allocator.RT_AllocateBuffer( "STAGING", bufferCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY,
         buffer);*/
    }

    void VulkanPipelineCompute::BindDS( VkDescriptorSet descriptorSet )
    {
        vkCmdBindDescriptorSets( m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 m_ComputePipelineLayout, 0, 1, &descriptorSet, 0, 0 );
    }

} // namespace Desert::Graphic::API::Vulkan