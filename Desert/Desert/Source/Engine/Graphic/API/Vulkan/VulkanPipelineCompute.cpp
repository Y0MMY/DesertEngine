#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanAllocator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>
#include <Engine/Core/EngineContext.hpp>

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

        static_cast<API::Vulkan::VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() )
             ->GetDescriptorManager()
             ->CleanupFrame( EngineContext::GetInstance().GetFramesInFlight() );
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

        const auto vulkanShader         = sp_cast<VulkanShader>( m_Shader );
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

        VK_CHECK_RESULT(
             vkCreatePipelineLayout( device, &pipelineLayoutCreateInfo, nullptr, &m_ComputePipelineLayout ) );

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        VK_CHECK_RESULT( vkCreatePipelineCache( device, &pipelineCacheCreateInfo, nullptr, &m_PipelineCache ) );

        VkComputePipelineCreateInfo pipelineInfo{ .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                                  .stage  = vulkanShader->GetPipelineShaderStageCreateInfos()[0],
                                                  .layout = m_ComputePipelineLayout };

        VK_CHECK_RESULT(
             vkCreateComputePipelines( device, m_PipelineCache, 1, &pipelineInfo, nullptr, &m_ComputePipeline ) );

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_PIPELINE, m_Shader->GetName(),
                                          m_ComputePipeline );
    }

    void VulkanPipelineCompute::ReadBuffer( uint32_t bufferSize )
    {
    }

    Common::Result<VkDescriptorSet> VulkanPipelineCompute::GetDescriptorSet( uint32_t frameIndex,
                                                                             uint32_t setIndex )
    {
        auto rendererAPI = static_cast<VulkanRendererAPI*>( Renderer::GetInstance().GetRendererAPI() );
        auto result = rendererAPI->GetDescriptorManager()->GetDescriptorSet( sp_cast<VulkanShader>( m_Shader ),
                                                                             frameIndex, setIndex );

        if ( result.IsSuccess() )
        {
            return Common::MakeSuccess( result.GetValue().Set );
        }
        return Common::MakeError<VkDescriptorSet>( "Failed to get descriptor set" );
    }

    void VulkanPipelineCompute::UpdateDescriptorSet( uint32_t                                 frameIndex,
                                                     const std::vector<VkWriteDescriptorSet>& writes,
                                                     VkDescriptorSet descriptorSet, uint32_t setIndex )
    {
        std::vector<VkWriteDescriptorSet> modifiedWrites = writes;
        for ( auto& write : modifiedWrites )
        {
            write.dstSet = descriptorSet;
        }

        vkUpdateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                static_cast<uint32_t>( modifiedWrites.size() ), modifiedWrites.data(), 0,
                                nullptr );
    }

    void VulkanPipelineCompute::BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex )
    {
        auto vulkanShader = sp_cast<VulkanShader>( m_Shader );
        vkCmdBindDescriptorSets( m_ActiveComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 m_ComputePipelineLayout, 0, 1, &descriptorSet, 0, nullptr );
    }

    void VulkanPipelineCompute::PushConstant( uint32_t size, void* data )
    {
        vkCmdPushConstants( m_ActiveComputeCommandBuffer, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            size, data );
    }

    void VulkanPipelineCompute::Release()
    {
        VkDevice device = VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice();

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

} // namespace Desert::Graphic::API::Vulkan