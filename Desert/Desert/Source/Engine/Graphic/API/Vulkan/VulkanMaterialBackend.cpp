#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

#include <Engine/ShaderResources/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImage2D.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp>
#include <Engine/ShaderResources/API/Vulkan/VulkanStorageBuffer.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanImage.hpp>
#include <Engine/Core/EngineContext.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/WriteDescriptorSetBuilder.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanMaterialBackend::VulkanMaterialBackend( const std::shared_ptr<Shader>& shader )
         : MaterialBackend( shader ), m_VulkanShader( SP_CAST( VulkanShader, shader ) )
    {
        CreateDescriptorPool();
        AllocateDescriptorSets();
    }

    VulkanMaterialBackend::~VulkanMaterialBackend()
    {
        if ( m_DescriptorPool != VK_NULL_HANDLE )
        {
            vkDestroyDescriptorPool( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetDevice() )
                                          ->GetVulkanLogicalDevice(),
                                     m_DescriptorPool, nullptr );
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

    void VulkanMaterialBackend::UpdateDescriptorSets( const std::vector<VkWriteDescriptorSet>& writes )
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

        const auto& frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

        if ( auto bufferInfo = uniformProp->GetUniform() )
        {
            if ( auto vulkanBuffer = sp_cast<ShaderResources::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
            {
                auto& descriptorBufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
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

        const auto& frameIndex = EngineContext::GetInstance().GetCurrentFrameIndex();

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

        const auto& frameIndex   = EngineContext::GetInstance().GetCurrentFrameIndex();

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

        const auto& frameIndex   = EngineContext::GetInstance().GetCurrentFrameIndex();

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
                setsToBind.push_back( descriptorSet );
        }

        if ( setsToBind.empty() )
            return;

        vkCmdBindDescriptorSets( cmdBuffer, bindPoint, layout, 0, static_cast<uint32_t>( setsToBind.size() ),
                                 setsToBind.data(), 0, nullptr );
    }

    bool VulkanMaterialBackend::HasDescriptorSets() const
    {
        return !m_DescriptorSets.empty() && !m_DescriptorSets[0].empty();
    }

    void VulkanMaterialBackend::FlushUpdates()
    {
        // No longer using a queue to avoid dangling pointer issues
    }

    void VulkanMaterialBackend::InitializeWithFallbacks()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetMaxFramesInFlight();
        auto&          descriptorSets = m_VulkanShader->GetShaderDescriptorSets();

        for ( uint32_t frame = 0; frame < framesInFlight; ++frame )
        {
            for ( const auto& [setIndex, descriptorSet] : descriptorSets )
            {
                // IMAGE 2D SAMPLERS
                for ( const auto& [binding, imageLayout] : descriptorSet.Image2DSamplers )
                {
                    auto fallbackImage =
                         FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA32F );

                    if ( auto vulkanImage = sp_cast<VulkanImage2D>( fallbackImage ) )
                    {
                        auto descriptorImageInfo = vulkanImage->GetResource().GetDescriptorInfo();
                        auto wds = DescriptorSetBuilder::GetSampler2DWDS( this, frame, setIndex, binding, 1,
                                                                          &descriptorImageInfo );
                        UpdateDescriptorSets( { wds } );
                    }
                }

                // IMAGE CUBE SAMPLERS
                for ( const auto& [binding, imageLayout] : descriptorSet.ImageCubeSamplers )
                {
                    auto fallbackCube =
                         FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F );

                    if ( auto vulkanImage = sp_cast<VulkanImageCube>( fallbackCube ) )
                    {
                        auto descriptorImageInfo = vulkanImage->GetResource().GetDescriptorInfo();
                        auto wds = DescriptorSetBuilder::GetSamplerCubeWDS( this, frame, setIndex, binding, 1,
                                                                            &descriptorImageInfo );
                        UpdateDescriptorSets( { wds } );
                    }
                }
            }
        }
    }

    void VulkanMaterialBackend::InitializeDefaults()
    {
        InitializeWithFallbacks();
    }

} // namespace Desert::Graphic::API::Vulkan
