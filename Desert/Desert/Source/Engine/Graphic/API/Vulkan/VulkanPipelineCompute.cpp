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
    VulkanPipelineCompute::VulkanPipelineCompute( const std::shared_ptr<Shader>& shader ) : m_Shader( shader )
    {
        m_VulkanMaterialBackend = std::make_unique<VulkanMaterialBackend>( shader );
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
        const auto& device        = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() );
        VkDevice    deviceLogical = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                      ->GetVulkanLogicalDevice();
        vkCreateFence( deviceLogical, &fenceCreateInfo, nullptr, &fence );
        VkQueue computeQueue = device->GetComputeQueue();
        vkQueueSubmit( computeQueue, 1, &submitInfo, fence );

        // Wait for the fence to signal that command buffer has finished executing
        VK_CHECK_RESULT( vkWaitForFences( deviceLogical, 1, &fence, VK_TRUE, UINT64_MAX ) );
        vkDestroyFence( deviceLogical, fence, nullptr );

        m_ActiveComputeCommandBuffer = VK_NULL_HANDLE;
    }

    void VulkanPipelineCompute::ExecuteMipLevel( const std::shared_ptr<Image>& imageForProccess, uint32_t mipLevel,
                                                 uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )
    {
        const auto& shader = m_Shader.lock();
        if ( !shader )
        {
            DESERT_VERIFY( false );
            return;
        }

        auto vulkanInputImage = dynamic_pointer_cast<VulkanImageBase>( imageForProccess );
        if ( !vulkanInputImage )
        {
            DESERT_VERIFY( false, "Failed to cast to VulkanImageBase" );
            return;
        }

        uint32_t    frameIndex     = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& inputImageInfo = vulkanInputImage->GetVulkanImageInfo();

        // Prepare descriptor image info
        std::array<VkDescriptorImageInfo, 2> imageInfo = {};

        // Input image (read from previous mip level)
        imageInfo[0].imageView   = ( mipLevel == 0 ) ? inputImageInfo.ImageInfo.imageView
                                                     : vulkanInputImage->GetMipImageView( mipLevel - 1 );
        imageInfo[0].sampler     = inputImageInfo.ImageInfo.sampler;
        imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Correct layout for sampling

        // Output image (write to current mip level)
        imageInfo[1].imageView   = vulkanInputImage->GetMipImageView( mipLevel );
        imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Correct layout for storage image

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.push_back( DescriptorSetBuilder::GetSamplerCubeWDS(
             m_VulkanMaterialBackend.get(), frameIndex, 0, 0, 1, &imageInfo[0] ) );
        descriptorWrites.push_back( DescriptorSetBuilder::GetStorageWDS( m_VulkanMaterialBackend.get(), frameIndex,
                                                                         0, 1, 1, &imageInfo[1] ) );

        // First command buffer: transition input image to SHADER_READ_ONLY_OPTIMAL
        // and output image to GENERAL
        {
            const auto& cmdBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
            if ( !cmdBuffer )
            {
                return;
            }

            VkImageSubresourceRange inputRange = {};
            inputRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
            inputRange.baseMipLevel            = ( mipLevel == 0 ) ? 0 : mipLevel - 1;
            inputRange.levelCount              = 1;
            inputRange.baseArrayLayer          = 0;
            inputRange.layerCount              = 6;

            VkImageSubresourceRange outputRange = {};
            outputRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
            outputRange.baseMipLevel            = mipLevel;
            outputRange.levelCount              = 1;
            outputRange.baseArrayLayer          = 0;
            outputRange.layerCount              = 6;

            // Transition input image to SHADER_READ_ONLY_OPTIMAL
            Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), inputImageInfo.Image,
                                             VK_ACCESS_SHADER_WRITE_BIT,           // Previous writes (if any)
                                             VK_ACCESS_SHADER_READ_BIT,            // We'll read from it
                                             inputImageInfo.ImageInfo.imageLayout, // Current layout
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, inputRange );

            // Transition output image to GENERAL for writing
            Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), inputImageInfo.Image,
                                             0, // No previous access
                                             VK_ACCESS_SHADER_WRITE_BIT,
                                             VK_IMAGE_LAYOUT_UNDEFINED, // Don't care about previous content
                                             VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, outputRange );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );
        }

        const auto& dSet = m_VulkanMaterialBackend->GetDescriptorSet( frameIndex );

        UpdateDescriptorSet( frameIndex, descriptorWrites, dSet );
        Begin();
        BindDescriptorSets( dSet, frameIndex );
        Dispatch( groupCountX, groupCountY, groupCountZ );
        End();

        // Second command buffer: transition output image back to appropriate layout
        {
            const auto& cmdBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
            if ( !cmdBuffer )
            {
                return;
            }

            VkImageSubresourceRange outputRange = {};
            outputRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
            outputRange.baseMipLevel            = mipLevel;
            outputRange.levelCount              = 1;
            outputRange.baseArrayLayer          = 0;
            outputRange.layerCount              = 6;

            // Transition output image from GENERAL to whatever layout it should be in
            Utils::InsertImageMemoryBarrier( cmdBuffer.GetValue(), inputImageInfo.Image,
                                             VK_ACCESS_SHADER_WRITE_BIT, // We wrote to it
                                             VK_ACCESS_SHADER_READ_BIT,  // Future reads
                                             VK_IMAGE_LAYOUT_GENERAL,
                                             inputImageInfo.ImageInfo.imageLayout, // Target layout
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, outputRange );

            CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );
        }
    }

    void VulkanPipelineCompute::Execute( const std::shared_ptr<Image>& imageForProccess,
                                         std::shared_ptr<Image>& outputImage, uint32_t groupCountX,
                                         uint32_t groupCountY, uint32_t groupCountZ )
    {
        const auto& shader = m_Shader.lock();
        if ( !shader )
        {
            DESERT_VERIFY( false );
        }
        auto vulkanInputImage  = dynamic_pointer_cast<VulkanImageBase>( imageForProccess );
        auto vulkanOutputImage = dynamic_pointer_cast<VulkanImageBase>( outputImage );

        if ( !vulkanInputImage || !vulkanOutputImage )
        {
            DESERT_VERIFY( false && "Failed to cast to VulkanImageBase" );
            return;
        }
        const auto& inputImageInfo  = vulkanInputImage->GetVulkanImageInfo();
        const auto& outputImageInfo = vulkanOutputImage->GetVulkanImageInfo();

        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        std::array<VkDescriptorImageInfo, 2> imageInfo = {};

        // Input image
        imageInfo[0].imageView   = inputImageInfo.ImageInfo.imageView;
        imageInfo[0].sampler     = inputImageInfo.ImageInfo.sampler;
        imageInfo[0].imageLayout = inputImageInfo.ImageInfo.imageLayout;

        // Output image
        imageInfo[1].imageView   = outputImageInfo.ImageInfo.imageView;
        imageInfo[1].imageLayout = outputImageInfo.ImageInfo.imageLayout;

        std::vector<VkWriteDescriptorSet> descriptorWrites;

        descriptorWrites.push_back( DescriptorSetBuilder::GetSamplerCubeWDS(
             m_VulkanMaterialBackend.get(), frameIndex, 0, 0, 1, &imageInfo[0] ) );

        descriptorWrites.push_back( DescriptorSetBuilder::GetStorageWDS( m_VulkanMaterialBackend.get(), frameIndex,
                                                                         0, 1, 1, &imageInfo[1] ) );
        const auto& dSet = m_VulkanMaterialBackend->GetDescriptorSet( frameIndex );

        UpdateDescriptorSet( frameIndex, descriptorWrites, dSet );

        Begin();
        BindDescriptorSets( dSet, frameIndex );
        Dispatch( groupCountX, groupCountY, groupCountZ );

        End();

        const auto& cmdBuffer = CommandBufferAllocator::GetInstance().RT_AllocateCommandBufferGraphic( true );
        if ( !cmdBuffer )
        {
            return;
        }

        vulkanOutputImage->TransitionImageLayout( cmdBuffer.GetValue(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferGraphic( cmdBuffer.GetValue() );
    }

    void VulkanPipelineCompute::Dispatch( uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ )
    {
        if ( m_StorageBuffer.Size )
        {
            vkCmdPushConstants( m_ActiveComputeCommandBuffer, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                                0, m_StorageBuffer.Size, m_StorageBuffer.Data );
        }
        vkCmdDispatch( m_ActiveComputeCommandBuffer, groupCountX, groupCountY, groupCountZ );
    }

    void VulkanPipelineCompute::Invalidate()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();

        const auto& shader = m_Shader.lock();
        if ( !shader )
        {
            return;
        }

        const auto vulkanShader         = sp_cast<VulkanShader>( shader );
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

        VKUtils::SetDebugUtilsObjectName( device, VK_OBJECT_TYPE_PIPELINE, shader->GetName(), m_ComputePipeline );
    }

    void VulkanPipelineCompute::BindDescriptorSets( VkDescriptorSet descriptorSet, uint32_t frameIndex )
    {
        m_VulkanMaterialBackend->BindDescriptorSets( m_ActiveComputeCommandBuffer, m_ComputePipelineLayout,
                                                     VK_PIPELINE_BIND_POINT_COMPUTE, frameIndex );
    }

    void VulkanPipelineCompute::Release()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
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

    void VulkanPipelineCompute::UpdateDescriptorSet( uint32_t                                 frameIndex,
                                                     const std::vector<VkWriteDescriptorSet>& writes,
                                                     VkDescriptorSet descriptorSet, uint32_t setIndex /*= 0 */ )
    {
        std::vector<VkWriteDescriptorSet> modifiedWrites = writes;
        for ( auto& write : modifiedWrites )
        {
            write.dstSet = descriptorSet;
        }
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                               ->GetVulkanLogicalDevice();
        vkUpdateDescriptorSets( device, static_cast<uint32_t>( modifiedWrites.size() ), modifiedWrites.data(), 0,
                                nullptr );
    }

} // namespace Desert::Graphic::API::Vulkan