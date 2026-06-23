#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

#include <limits>
#include <cstring>
#include <Engine/ShaderResources/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImage2D.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Core/FrameManager.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanMaterialBackend::VulkanMaterialBackend( const std::shared_ptr<Shader>& shader )
         : MaterialBackend( shader ), m_VulkanShader( SP_CAST( VulkanShader, shader ) )
    {
        CreateDescriptorPool();
        AllocateDescriptorSets();

        // Create a dummy buffer to initialize unused bindings
        VmaAllocator allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )
                                      ->GetVulkanAllocator()
                                      ->GetVMAAllocator();

        VkBufferCreateInfo      bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                               .size  = 65536,
                                               .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        // Host-visible + zero-initialized. Every uniform/storage binding starts out pointing at this
        // dummy buffer (see InitializeWithFallbacks). If it held uninitialized GPU memory, any binding
        // sampled before its real resource is bound — or one that is never bound — would read garbage,
        // which the lighting blows up into white/NaN (or near-zero black). Defined zeros make those
        // cases render as a stable black instead of flickering garbage.
        VmaAllocationCreateInfo allocInfo  = { .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                               .usage = VMA_MEMORY_USAGE_CPU_TO_GPU };

        VmaAllocationInfo dummyAllocInfo{};
        VK_CHECK_RESULT( vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &m_DummyBuffer, &m_DummyAllocation, &dummyAllocInfo ) );

        if ( dummyAllocInfo.pMappedData )
        {
            std::memset( dummyAllocInfo.pMappedData, 0, bufferInfo.size );
        }
    }

    VulkanMaterialBackend::~VulkanMaterialBackend()
    {
        VkDevice device = SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                               ->GetVulkanLogicalDevice();

        if ( m_DescriptorPool != VK_NULL_HANDLE )
        {
            vkDestroyDescriptorPool( device, m_DescriptorPool, nullptr );
        }

        if ( m_DummyBuffer != VK_NULL_HANDLE )
        {
            VmaAllocator allocator = SP_CAST( VulkanContext, EngineContext::GetInstance().GetRendererContext() )
                                          ->GetVulkanAllocator()
                                          ->GetVMAAllocator();
            vmaDestroyBuffer( allocator, m_DummyBuffer, m_DummyAllocation );
        }
    }

    void VulkanMaterialBackend::CreateDescriptorPool()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        const uint32_t setCount       = m_VulkanShader->GetDescriptorSetLayoutCount();

        auto& descriptorSets = m_VulkanShader->GetShaderDescriptorSets();

        uint32_t uniformBufferCount        = 0;
        uint32_t combinedImageSamplerCount = 0;
        uint32_t storageImageCount         = 0;
        uint32_t storageBufferCount        = 0;

        for ( const auto& [setIndex, descriptorSet] : descriptorSets )
        {
            uniformBufferCount += (uint32_t)descriptorSet.UniformBuffers.size();
            combinedImageSamplerCount += (uint32_t)descriptorSet.Image2DSamplers.size();
            combinedImageSamplerCount += (uint32_t)descriptorSet.ImageCubeSamplers.size();
            storageImageCount += (uint32_t)descriptorSet.StorageImage2DSamplers.size();
            storageBufferCount += (uint32_t)descriptorSet.StorageBuffers.size();
        }

        uniformBufferCount *= framesInFlight;
        combinedImageSamplerCount *= framesInFlight;
        storageImageCount *= framesInFlight;
        storageBufferCount *= framesInFlight;

        std::vector<VkDescriptorPoolSize> poolSizes;
        if ( uniformBufferCount > 0 ) poolSizes.push_back( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount } );
        if ( combinedImageSamplerCount > 0 ) poolSizes.push_back( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combinedImageSamplerCount } );
        if ( storageImageCount > 0 ) poolSizes.push_back( { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount } );
        if ( storageBufferCount > 0 ) poolSizes.push_back( { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount } );

        if ( poolSizes.empty() ) poolSizes.push_back( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } );

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>( poolSizes.size() );
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = framesInFlight * setCount;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RESULT( vkCreateDescriptorPool( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                                          ->GetVulkanLogicalDevice(), &poolInfo, nullptr, &m_DescriptorPool ) );
    }

    void VulkanMaterialBackend::AllocateDescriptorSets()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        const uint32_t setCount       = m_VulkanShader->GetDescriptorSetLayoutCount();

        m_DescriptorSets.resize( framesInFlight );
        m_DescriptorSetsUpdateFrame.resize( framesInFlight, std::vector<uint64_t>( setCount, std::numeric_limits<uint64_t>::max() ) );

        for ( uint32_t frame = 0; frame < framesInFlight; ++frame )
        {
            m_DescriptorSets[frame].resize( setCount );

            std::vector<VkDescriptorSetLayout> layouts( setCount );
            for ( uint32_t set = 0; set < setCount; ++set )
            {
                layouts[set] = m_VulkanShader->GetDescriptorSetLayout( set );
            }

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool     = m_DescriptorPool;
            allocInfo.descriptorSetCount = setCount;
            allocInfo.pSetLayouts        = layouts.data();

            VK_CHECK_RESULT( vkAllocateDescriptorSets(
                 SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                      ->GetVulkanLogicalDevice(),
                 &allocInfo, m_DescriptorSets[frame].data() ) );
        }
    }

    VkDescriptorSet VulkanMaterialBackend::GetDescriptorSet( uint32_t frameIndex, uint32_t setIndex ) const
    {
        if ( frameIndex < m_DescriptorSets.size() && setIndex < m_DescriptorSets[frameIndex].size() )
        {
            return m_DescriptorSets[frameIndex][setIndex];
        }
        return VK_NULL_HANDLE;
    }

    void VulkanMaterialBackend::ApplyPushConstants( MaterialExecutor* material, GraphicsPipeline* pipeline )
    {
    }

    void VulkanMaterialBackend::UpdateDescriptorSets( const std::vector<VkWriteDescriptorSet>& writes, bool force )
    {
        if ( writes.empty() ) return;
        
        vkUpdateDescriptorSets( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                                     ->GetVulkanLogicalDevice(),
                                static_cast<uint32_t>( writes.size() ),
                                writes.data(), 0, nullptr );
    }

    void VulkanMaterialBackend::ApplyUniformBuffer( MaterialProperty* prop )
    {
        auto uniformProp = static_cast<UniformBufferProperty*>( prop );
        if ( !uniformProp || !uniformProp->IsDirty() )
            return;

        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();
        const uint64_t absoluteFrame = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
        const uint32_t setIndex = 0; // Simplified

        // Only update if not already updated this absolute frame
        if ( m_DescriptorSetsUpdateFrame[frameIndex][setIndex] == absoluteFrame )
            return;

        if ( auto bufferInfo = uniformProp->GetUniform() )
        {
            if ( auto vulkanBuffer = sp_cast<ShaderResources::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
            {
                auto& descriptorBufferInfo = vulkanBuffer->GetDescriptorBufferInfo( frameIndex );
                auto  wds = DescriptorSetBuilder::GetUniformWDS( this, frameIndex, 0,
                                                                 vulkanBuffer->GetBinding(), 1U, &descriptorBufferInfo );

                UpdateDescriptorSets( { wds } );
                uniformProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyStorageBuffer( MaterialProperty* prop )
    {
        auto storageProp = static_cast<StorageBufferProperty*>( prop );
        if ( !storageProp || !storageProp->IsDirty() )
            return;

        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();
        const uint64_t absoluteFrame = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
        const uint32_t setIndex = 0; // Simplified

        if ( m_DescriptorSetsUpdateFrame[frameIndex][setIndex] == absoluteFrame )
            return;

        if ( auto bufferInfo = storageProp->GetStorageBuffer() )
        {
            if ( auto vulkanBuffer = sp_cast<ShaderResources::API::Vulkan::VulkanStorageBuffer>( bufferInfo ) )
            {
                auto& descriptorBufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                auto  wds = DescriptorSetBuilder::GetStorageWDS( this, frameIndex, 0,
                                                                 vulkanBuffer->GetBinding(), 1U, &descriptorBufferInfo );

                UpdateDescriptorSets( { wds } );
                storageProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTexture2D( MaterialProperty* prop )
    {
        auto textureProp = static_cast<Texture2DProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const uint32_t frameIndex   = EngineContext::GetInstance().GetCurrentFrameIndex();
        const uint64_t absoluteFrame = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
        const uint32_t setIndex = 0; // Simplified

        if ( m_DescriptorSetsUpdateFrame[frameIndex][setIndex] == absoluteFrame )
            return;

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<ShaderResources::API::Vulkan::VulkanUniformImage2D>( imageUniform ) )
            {
                auto descriptorImageInfo = vulkanImage->GetDescriptorImageInfo();
                auto wds = DescriptorSetBuilder::GetSampler2DWDS( this, frameIndex, 0,
                                                                  vulkanImage->GetBinding(), 1U, &descriptorImageInfo );

                UpdateDescriptorSets( { wds } );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTextureCube( MaterialProperty* prop )
    {
        auto textureProp = static_cast<TextureCubeProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const uint32_t frameIndex   = EngineContext::GetInstance().GetCurrentFrameIndex();
        const uint64_t absoluteFrame = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
        const uint32_t setIndex = 0; // Simplified

        if ( m_DescriptorSetsUpdateFrame[frameIndex][setIndex] == absoluteFrame )
            return;

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<ShaderResources::API::Vulkan::VulkanUniformImageCube>( imageUniform ) )
            {
                auto descriptorImageInfo = vulkanImage->GetDescriptorImageInfo();
                auto wds = DescriptorSetBuilder::GetSamplerCubeWDS( this, frameIndex, 0,
                                                                    vulkanImage->GetBinding(), 1U, &descriptorImageInfo );

                UpdateDescriptorSets( { wds } );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineLayout layout,
                                                    VkPipelineBindPoint bindPoint, uint32_t frameIndex )
    {
        if ( frameIndex >= m_DescriptorSets.size() || m_DescriptorSets[frameIndex].empty() )
            return;

        std::vector<VkDescriptorSet> setsToBind;
        setsToBind.reserve( m_DescriptorSets[frameIndex].size() );

        for ( VkDescriptorSet descriptorSet : m_DescriptorSets[frameIndex] )
        {
            if ( descriptorSet != VK_NULL_HANDLE )
            {
                setsToBind.push_back( descriptorSet );
            }
            else
            {
                // Instead of falling back or just warning, we log a critical error and skip
                // binding this entire set to prevent vkCmdBindDescriptorSets from crashing.
                LOG_ERROR( "VulkanMaterialBackend: Attempting to bind NULL descriptor set! This indicates a failure in descriptor set initialization or asset loading." );
                return;
            }
        }

        vkCmdBindDescriptorSets( cmdBuffer, bindPoint, layout, 0, static_cast<uint32_t>( setsToBind.size() ),
                                 setsToBind.data(), 0, nullptr );
    }

    void VulkanMaterialBackend::ResetFrameUpdateState( uint32_t frameIndex )
    {
    }

    bool VulkanMaterialBackend::HasDescriptorSets() const
    {
        return !m_DescriptorSets.empty() && !m_DescriptorSets[0].empty();
    }

    void VulkanMaterialBackend::FlushUpdates()
    {
        const uint32_t frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();
        const uint64_t absoluteFrame = Engine::FrameManager::GetInstance().GetAbsoluteFrameCount();
        
        // Mark all sets as updated for this absolute frame
        for ( uint32_t i = 0; i < m_DescriptorSetsUpdateFrame[frameIndex].size(); ++i )
            m_DescriptorSetsUpdateFrame[frameIndex][i] = absoluteFrame;
    }

    void VulkanMaterialBackend::InitializeWithFallbacks()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        auto&          descriptorSets = m_VulkanShader->GetShaderDescriptorSets();

        for ( uint32_t frame = 0; frame < framesInFlight; ++frame )
        {
            for ( const auto& [setIndex, descriptorSet] : descriptorSets )
            {
                std::vector<VkWriteDescriptorSet> writes;
                
                // Track infos to keep them alive until vkUpdateDescriptorSets
                std::vector<VkDescriptorImageInfo> imageInfos;
                imageInfos.reserve( descriptorSet.Image2DSamplers.size() +
                                    descriptorSet.ImageCubeSamplers.size() +
                                    descriptorSet.StorageImage2DSamplers.size() );

                std::vector<VkDescriptorBufferInfo> bufferInfos;
                bufferInfos.reserve(descriptorSet.UniformBuffers.size() + descriptorSet.StorageBuffers.size());

                // UNIFORM BUFFERS
                for ( const auto& [binding, size] : descriptorSet.UniformBuffers )
                {
                    VkDescriptorBufferInfo info = { .buffer = m_DummyBuffer, .offset = 0, .range = VK_WHOLE_SIZE };
                    bufferInfos.push_back( info );
                    writes.push_back( DescriptorSetBuilder::GetUniformWDS( this, frame, setIndex, binding, 1,
                                                                           &bufferInfos.back() ) );
                }

                // STORAGE BUFFERS
                for ( const auto& [binding, size] : descriptorSet.StorageBuffers )
                {
                    VkDescriptorBufferInfo info = { .buffer = m_DummyBuffer, .offset = 0, .range = VK_WHOLE_SIZE };
                    bufferInfos.push_back( info );
                    writes.push_back( DescriptorSetBuilder::GetStorageWDS( this, frame, setIndex, binding, 1,
                                                                           &bufferInfos.back() ) );
                }

                // IMAGE 2D SAMPLERS
                for ( const auto& [binding, imageLayout] : descriptorSet.Image2DSamplers )
                {
                    auto fallbackImage =
                         FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA32F );

                    if ( auto vulkanImage = sp_cast<VulkanImage2D>( fallbackImage ) )
                    {
                        imageInfos.push_back(vulkanImage->GetResource().GetDescriptorInfo());
                        writes.push_back(DescriptorSetBuilder::GetSampler2DWDS( this, frame, setIndex, binding, 1,
                                                                          &imageInfos.back() ));
                    }
                }

                // IMAGE CUBE SAMPLERS
                for ( const auto& [binding, imageLayout] : descriptorSet.ImageCubeSamplers )
                {
                    auto fallbackCube =
                         FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F );

                    if ( auto vulkanImage = sp_cast<VulkanImageCube>( fallbackCube ) )
                    {
                        imageInfos.push_back(vulkanImage->GetResource().GetDescriptorInfo());
                        writes.push_back(DescriptorSetBuilder::GetSamplerCubeWDS( this, frame, setIndex, binding, 1,
                                                                            &imageInfos.back() ));
                    }
                }

                // STORAGE IMAGES — init with dedicated storage fallback (has VK_IMAGE_USAGE_STORAGE_BIT)
                for ( const auto& [binding, _] : descriptorSet.StorageImage2DSamplers )
                {
                    auto fallbackImage =
                         FallbackTextures::Get().GetFallbackStorageImage2D( Core::Formats::ImageFormat::RGBA32F );

                    if ( auto vulkanImage = sp_cast<VulkanImage2D>( fallbackImage ) )
                    {
                        VkDescriptorImageInfo storageInfo = { VK_NULL_HANDLE,
                                                              vulkanImage->GetResource().ImageView,
                                                              VK_IMAGE_LAYOUT_GENERAL };
                        imageInfos.push_back( storageInfo );
                        writes.push_back( DescriptorSetBuilder::GetStorageWDS( this, frame, setIndex, binding, 1,
                                                                               &imageInfos.back() ) );
                    }
                }

                UpdateDescriptorSets( writes, true );
            }
        }
    }

    void VulkanMaterialBackend::InitializeDefaults()
    {
        InitializeWithFallbacks();
    }

} // namespace Desert::Graphic::API::Vulkan
