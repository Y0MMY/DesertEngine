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
#include <Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp>

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

    ComputePipeline& VulkanPipelineCompute::SetInput( uint32_t binding, Image* image )
    {
        m_BoundInputs[binding] = image;
        return *this;
    }

    ComputePipeline& VulkanPipelineCompute::SetOutput( uint32_t binding, Image* image, uint32_t mip )
    {
        m_BoundOutputs[binding] = { image, mip };
        return *this;
    }

    ComputePipeline& VulkanPipelineCompute::SetStorageBuffer( uint32_t                        binding,
                                                              ShaderResources::StorageBuffer* buffer )
    {
        m_BoundStorageBuffers[binding] = buffer;
        return *this;
    }

    ComputePipeline& VulkanPipelineCompute::SetPushConstants( const void* data, uint32_t size )
    {
        const auto* bytes = reinterpret_cast<const std::byte*>( data );
        m_BoundPushConstants.assign( bytes, bytes + size );
        return *this;
    }

    Image* VulkanPipelineCompute::GetInput( uint32_t binding ) const
    {
        const auto it = m_BoundInputs.find( binding );
        return it != m_BoundInputs.end() ? it->second : nullptr;
    }

    Image* VulkanPipelineCompute::GetOutput( uint32_t binding ) const
    {
        const auto it = m_BoundOutputs.find( binding );
        return it != m_BoundOutputs.end() ? it->second.Image : nullptr;
    }

    void VulkanPipelineCompute::RecordDescriptorsAndDispatch( VkCommandBuffer cmd, VkDescriptorSet descriptorSet,
                                                              uint32_t groupsX, uint32_t groupsY,
                                                              uint32_t groupsZ )
    {
        // The image infos must outlive UpdateDescriptorSet (each write stores a pointer into here),
        // so reserve up front to avoid a reallocation invalidating those pointers.
        std::vector<VkDescriptorImageInfo> infos;
        infos.reserve( m_BoundInputs.size() + m_BoundOutputs.size() );
        std::vector<VkWriteDescriptorSet> writes;

        // Outputs: bind the chosen mip view as a storage image (the caller has already put the image
        // into GENERAL — we never transition here so this works both immediate and in-frame).
        for ( const auto& [binding, out] : m_BoundOutputs )
        {
            auto* img = dynamic_cast<IVulkanImage*>( out.Image );
            if ( !img )
                continue;
            infos.push_back( { VK_NULL_HANDLE, img->GetMipView( out.Mip ), VK_IMAGE_LAYOUT_GENERAL } );
            writes.push_back( DescriptorSetBuilder::GetStorageWDS( m_VulkanMaterialBackend.get(), 0, 0,
                                                                  binding, 1, &infos.back() ) );
        }

        // Inputs: sampled images (2D or cube share the same VkDescriptorImageInfo shape). Use the
        // image's actual tracked layout — a sampled input may be SHADER_READ_ONLY (a separate source
        // image) or GENERAL (a mip of an image currently being written by this same chain).
        for ( const auto& [binding, image] : m_BoundInputs )
        {
            auto* img = dynamic_cast<IVulkanImage*>( image );
            if ( !img )
                continue;
            const auto& r = img->GetResource();
            infos.push_back( { r.Sampler, r.ImageView, r.Layout } );
            writes.push_back( DescriptorSetBuilder::GetSampler2DWDS( m_VulkanMaterialBackend.get(), 0, 0,
                                                                    binding, 1, &infos.back() ) );
        }

        // Storage buffers (read-write; e.g. a luminance histogram). The descriptor buffer info lives in
        // the buffer object (stable across this call), so we can point the write straight at it.
        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();
        for ( const auto& [binding, buffer] : m_BoundStorageBuffers )
        {
            auto* vkBuffer = dynamic_cast<ShaderResources::API::Vulkan::VulkanStorageBuffer*>( buffer );
            if ( !vkBuffer )
                continue;
            writes.push_back( DescriptorSetBuilder::GetStorageWDS( m_VulkanMaterialBackend.get(), 0, 0, binding,
                                                                  1, &vkBuffer->GetDescriptorBufferInfo( frameIndex ) ) );
        }

        // Retarget every write at the supplied set, then update it in one shot.
        UpdateDescriptorSet( 0, writes, descriptorSet );

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipeline );
        vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_ComputePipelineLayout, 0, 1,
                                 &descriptorSet, 0, nullptr );

        if ( !m_BoundPushConstants.empty() )
            vkCmdPushConstants( cmd, m_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                static_cast<uint32_t>( m_BoundPushConstants.size() ),
                                m_BoundPushConstants.data() );

        vkCmdDispatch( cmd, groupsX, groupsY, groupsZ );
    }

    void VulkanPipelineCompute::Dispatch( uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ )
    {
        auto* backend = GetVulkanMaterialBackend();
        if ( !backend )
            return;

        auto cmdResult = CommandBufferAllocator::GetInstance().RT_GetCommandBufferCompute( true );
        if ( !cmdResult.IsSuccess() )
            return;
        VkCommandBuffer cmd = cmdResult.GetValue();

        // Immediate path: transition outputs to GENERAL, dispatch, transition back to SHADER_READ so the
        // result can be sampled, then submit + wait. Reuses the persistent set 0 (safe — we wait below).
        for ( const auto& [binding, out] : m_BoundOutputs )
            if ( auto* img = dynamic_cast<IVulkanImage*>( out.Image ) )
                img->TransitionLayout( cmd, VK_IMAGE_LAYOUT_GENERAL );

        RecordDescriptorsAndDispatch( cmd, backend->GetDescriptorSet( 0, 0 ), groupsX, groupsY, groupsZ );

        for ( const auto& [binding, out] : m_BoundOutputs )
            if ( auto* img = dynamic_cast<IVulkanImage*>( out.Image ) )
                img->TransitionLayout( cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

        CommandBufferAllocator::GetInstance().RT_FlushCommandBufferCompute( cmd );
    }

    void VulkanPipelineCompute::EnsureInFrameRing()
    {
        if ( m_InFramePool != VK_NULL_HANDLE )
            return;

        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                              ->GetVulkanLogicalDevice();

        // Pool sized for the whole ring: one set per dispatch, each holding this shader's resources.
        const auto vulkanShader = sp_cast<VulkanShader>( m_Specification.Shader );

        uint32_t combinedImageSamplerCount = 0;
        uint32_t storageImageCount         = 0;
        uint32_t storageBufferCount        = 0;
        uint32_t uniformBufferCount        = 0;
        for ( const auto& [setIndex, descriptorSet] : vulkanShader->GetShaderDescriptorSets() )
        {
            combinedImageSamplerCount += (uint32_t)descriptorSet.Image2DSamplers.size();
            combinedImageSamplerCount += (uint32_t)descriptorSet.ImageCubeSamplers.size();
            storageImageCount += (uint32_t)descriptorSet.StorageImage2DSamplers.size();
            storageBufferCount += (uint32_t)descriptorSet.StorageBuffers.size();
            uniformBufferCount += (uint32_t)descriptorSet.UniformBuffers.size();
        }

        std::vector<VkDescriptorPoolSize> poolSizes;
        if ( combinedImageSamplerCount > 0 )
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   combinedImageSamplerCount * kInFrameRingSize } );
        if ( storageImageCount > 0 )
            poolSizes.push_back(
                 { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount * kInFrameRingSize } );
        if ( storageBufferCount > 0 )
            poolSizes.push_back(
                 { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount * kInFrameRingSize } );
        if ( uniformBufferCount > 0 )
            poolSizes.push_back(
                 { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount * kInFrameRingSize } );
        if ( poolSizes.empty() )
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kInFrameRingSize } );

        VkDescriptorPoolCreateInfo poolInfo{ .sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                             .maxSets = kInFrameRingSize,
                                             .poolSizeCount = static_cast<uint32_t>( poolSizes.size() ),
                                             .pPoolSizes    = poolSizes.data() };
        VK_CHECK_RESULT( vkCreateDescriptorPool( device, &poolInfo, nullptr, &m_InFramePool ) );

        VkDescriptorSetLayout layout0 = vulkanShader->GetDescriptorSetLayout( 0 );

        std::vector<VkDescriptorSetLayout> layouts( kInFrameRingSize, layout0 );
        m_InFrameRing.resize( kInFrameRingSize );
        VkDescriptorSetAllocateInfo allocInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                               .descriptorPool     = m_InFramePool,
                                               .descriptorSetCount = kInFrameRingSize,
                                               .pSetLayouts        = layouts.data() };
        VK_CHECK_RESULT( vkAllocateDescriptorSets( device, &allocInfo, m_InFrameRing.data() ) );
    }

    void VulkanPipelineCompute::RecordInFrame( VkCommandBuffer cmd, uint32_t groupsX, uint32_t groupsY,
                                               uint32_t groupsZ )
    {
        if ( !m_VulkanMaterialBackend || !cmd )
            return;

        EnsureInFrameRing();

        VkDescriptorSet set = m_InFrameRing[m_InFrameCursor];
        m_InFrameCursor     = ( m_InFrameCursor + 1 ) % kInFrameRingSize;

        RecordDescriptorsAndDispatch( cmd, set, groupsX, groupsY, groupsZ );
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

        // Must outlive vkCreatePipelineLayout: pPushConstantRanges is read at the call below, so this
        // cannot live inside the if-block (a dangling stack pointer here reads garbage sizes in Release).
        VkPushConstantRange vulkanPushConstantRange{};

        const auto& pushConstantRange = vulkanShader->GetShaderPushConstant();
        if ( pushConstantRange )
        {
            vulkanPushConstantRange = { .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
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

        if ( m_InFramePool != VK_NULL_HANDLE )
        {
            // Frees every ring set allocated from it.
            vkDestroyDescriptorPool( device, m_InFramePool, nullptr );
            m_InFramePool = VK_NULL_HANDLE;
            m_InFrameRing.clear();
            m_InFrameCursor = 0;
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
