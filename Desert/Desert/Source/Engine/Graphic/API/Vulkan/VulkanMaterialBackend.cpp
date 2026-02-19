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

            vkDestroyDescriptorPool( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                          ->GetVulkanLogicalDevice(),
                                     m_DescriptorPool, nullptr );
        }
    }

    void VulkanMaterialBackend::CreateDescriptorPool()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetFramesInFlight();
        const uint32_t setCount       = m_VulkanShader->GetDescriptorSetLayoutCount();

        auto& descriptorSets = m_VulkanShader->GetShaderDescriptorSets();

        uint32_t uniformBufferCount        = 0;
        uint32_t combinedImageSamplerCount = 0;
        uint32_t storageImageCount         = 0;
        uint32_t storageBufferCount        = 0;

        for ( const auto& [setIndex, descriptorSet] : descriptorSets )
        {
            // Uniform buffers
            uniformBufferCount += descriptorSet.UniformBuffers.size();

            // Combined image samplers (2D + Cube)
            combinedImageSamplerCount += descriptorSet.Image2DSamplers.size();
            combinedImageSamplerCount += descriptorSet.ImageCubeSamplers.size();

            // Storage images
            storageImageCount += descriptorSet.StorageImage2DSamplers.size();

            // Storage buffers
            storageBufferCount += descriptorSet.StorageBuffers.size();
        }

        uniformBufferCount *= framesInFlight;
        combinedImageSamplerCount *= framesInFlight;
        storageImageCount *= framesInFlight;
        storageBufferCount *= framesInFlight;

        std::vector<VkDescriptorPoolSize> poolSizes;

        if ( uniformBufferCount > 0 )
        {
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniformBufferCount } );
        }

        if ( combinedImageSamplerCount > 0 )
        {
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, combinedImageSamplerCount } );
        }

        if ( storageImageCount > 0 )
        {
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, storageImageCount } );
        }

        if ( storageBufferCount > 0 )
        {
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount } );
        }

        if ( poolSizes.empty() )
        {
            poolSizes.push_back( { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } );
        }

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>( poolSizes.size() );
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = framesInFlight * setCount;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RESULT(
             vkCreateDescriptorPool( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                          ->GetVulkanLogicalDevice(),
                                     &poolInfo, nullptr, &m_DescriptorPool ) );
    }

    void VulkanMaterialBackend::AllocateDescriptorSets()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetFramesInFlight();
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
                 SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
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

    void VulkanMaterialBackend::ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline )
    {
    }

    void VulkanMaterialBackend::ApplyUniformBuffer( MaterialProperty* prop )
    {
        auto uniformProp = static_cast<UniformBufferProperty*>( prop );
        if ( !uniformProp || !uniformProp->IsDirty() )
            return;

        const auto& frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        if ( auto bufferInfo = uniformProp->GetUniform() )
        {
            if ( auto vulkanBuffer = sp_cast<ShaderResources::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
            {
                auto& bufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                auto  wds        = DescriptorSetBuilder::GetUniformWDS( this, frameIndex, 0, // set 0
                                                                        vulkanBuffer->GetBinding(), 1U, &bufferInfo );

                m_PendingDescriptorWrites.push_back( wds );
                uniformProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyStorageBuffer( MaterialProperty* prop )
    {
        auto storageProp = static_cast<StorageBufferProperty*>( prop );
        if ( !storageProp || !storageProp->IsDirty() )
            return;

        const auto& frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        if ( auto bufferInfo = storageProp->GetStorageBuffer() )
        {
            if ( auto vulkanBuffer = sp_cast<ShaderResources::API::Vulkan::VulkanStorageBuffer>( bufferInfo ) )
            {
                auto& bufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                auto  wds        = DescriptorSetBuilder::GetStorageWDS( this, frameIndex, 0, // set 0
                                                                        vulkanBuffer->GetBinding(), 1U, &bufferInfo );

                m_PendingDescriptorWrites.push_back( wds );
                storageProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTexture2D( MaterialProperty* prop )
    {
        auto textureProp = static_cast<Texture2DProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<ShaderResources::API::Vulkan::VulkanUniformImage2D>( imageUniform ) )
            {
                auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                auto  wds       = DescriptorSetBuilder::GetSampler2DWDS( this, frameIndex, 0, // set 0
                                                                         vulkanImage->GetBinding(), 1U, &imageInfo );

                m_PendingDescriptorWrites.push_back( wds );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::ApplyTextureCube( MaterialProperty* prop )
    {
        auto textureProp = static_cast<TextureCubeProperty*>( prop );
        if ( !textureProp || !textureProp->IsDirty() )
            return;

        const auto& frameIndex   = Renderer::GetInstance().GetCurrentFrameIndex();
        const auto& vulkanShader = std::static_pointer_cast<VulkanShader>( m_Shader );

        if ( auto imageUniform = textureProp->GetUniform() )
        {
            if ( auto vulkanImage = sp_cast<ShaderResources::API::Vulkan::VulkanUniformImageCube>( imageUniform ) )
            {
                auto& imageInfo = vulkanImage->GetDescriptorImageInfo();
                auto  wds       = DescriptorSetBuilder::GetSamplerCubeWDS( this, frameIndex, 0, // set 0
                                                                           vulkanImage->GetBinding(), 1U, &imageInfo );

                m_PendingDescriptorWrites.push_back( wds );
                textureProp->MarkClean();
            }
        }
    }

    void VulkanMaterialBackend::BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineLayout layout,
                                                    VkPipelineBindPoint bindPoint, uint32_t frameIndex )
    {
        if ( frameIndex >= m_DescriptorSets.size() || m_DescriptorSets[frameIndex].empty() )
        {
            return;
        }

        std::vector<VkDescriptorSet> setsToBind;
        setsToBind.reserve( m_DescriptorSets[frameIndex].size() );

        for ( VkDescriptorSet descriptorSet : m_DescriptorSets[frameIndex] )
        {
            if ( descriptorSet != VK_NULL_HANDLE )
            {
                setsToBind.push_back( descriptorSet );
            }
        }

        if ( setsToBind.empty() )
        {
            return;
        }

        vkCmdBindDescriptorSets( cmdBuffer, bindPoint, layout, 0, static_cast<uint32_t>( setsToBind.size() ),
                                 setsToBind.data(), 0, nullptr );
    }

    bool VulkanMaterialBackend::HasDescriptorSets() const
    {
        return !m_DescriptorSets.empty() && !m_DescriptorSets[0].empty();
    }

    void VulkanMaterialBackend::FlushUpdates()
    {
        if ( !m_PendingDescriptorWrites.empty() )
        {
            vkUpdateDescriptorSets( SP_CAST( VulkanLogicalDevice, EngineContext::GetInstance().GetMainDevice() )
                                         ->GetVulkanLogicalDevice(),
                                    static_cast<uint32_t>( m_PendingDescriptorWrites.size() ),
                                    m_PendingDescriptorWrites.data(), 0, nullptr );
            m_PendingDescriptorWrites.clear();
        }

        m_PendingDescriptorWrites.clear();
    }

    void VulkanMaterialBackend::InitializeWithFallbacks()
    {
        const uint32_t framesInFlight = EngineContext::GetInstance().GetFramesInFlight();
        auto&          descriptorSets = m_VulkanShader->GetShaderDescriptorSets();

        for ( uint32_t frame = 0; frame < framesInFlight; ++frame )
        {
            for ( const auto& [setIndex, descriptorSet] : descriptorSets )
            {
                // =========================================================
                // UNIFORM BUFFERS
                // =========================================================
               /* for ( const auto& [binding, ubLayout] : descriptorSet.UniformBuffers )
                {
                    Desert::ShaderResources::ShaderLayout::UniformBuffer fallbackLayout = ubLayout;
                    fallbackLayout.Size = std::max<uint32_t>( 1, ubLayout.Size );

                    static auto fallbackBuffer =
                         std::make_shared<ShaderResources::API::Vulkan::VulkanUniformBuffer>( fallbackLayout );

                    auto& bufferInfo = fallbackBuffer->GetDescriptorBufferInfo();

                    auto wds =
                         DescriptorSetBuilder::GetUniformWDS( this, frame, setIndex, binding, 1, &bufferInfo );

                    m_PendingDescriptorWrites.push_back( wds );
                }*/

                // =========================================================
                // STORAGE BUFFERS
                // =========================================================
                for ( const auto& [binding, sbLayout] : descriptorSet.StorageBuffers )
                {
                    /* Desert::ShaderResources::ShaderLayout::StorageBuffer fallbackLayout = sbLayout;
                     fallbackLayout.Size = std::max<uint32_t>( 1, sbLayout.Size );

                     auto fallbackBuffer =
                          std::make_shared<ShaderResources::API::Vulkan::VulkanStorageBuffer>( fallbackLayout );

                     auto& bufferInfo = fallbackBuffer->GetDescriptorBufferInfo();

                     auto wds =
                          DescriptorSetBuilder::GetStorageWDS( this, frame, setIndex, binding, 1, &bufferInfo );

                     m_PendingDescriptorWrites.push_back( wds );*/
                }

                // =========================================================
                // IMAGE 2D SAMPLERS
                // =========================================================
                for ( const auto& [binding, imageLayout] : descriptorSet.Image2DSamplers )
                {
                    static   auto fallbackImage =
                         FallbackTextures::Get().GetFallbackTexture2D( Core::Formats::ImageFormat::RGBA32F );

                    if ( auto vulkanImage = sp_cast<VulkanImage2D>( fallbackImage ) )
                    {
                        auto& bufferInfo = vulkanImage->GetVulkanImageInfo();

                        auto wds = DescriptorSetBuilder::GetSampler2DWDS( this, frame, setIndex, binding, 1,
                                                                          &bufferInfo.ImageInfo );

                        m_PendingDescriptorWrites.push_back( wds );
                    }

                    else
                    {
                        DESERT_VERIFY( false );
                    }
                }

                // =========================================================
                // IMAGE CUBE SAMPLERS
                // =========================================================
                for ( const auto& [binding, imageLayout] : descriptorSet.ImageCubeSamplers )
                {
                    static   auto fallbackCube =
                         FallbackTextures::Get().GetFallbackTextureCube( Core::Formats::ImageFormat::RGBA8F );

                    if ( auto vulkanImage = sp_cast<VulkanImageCube>( fallbackCube ) )
                    {
                        auto& bufferInfo = vulkanImage->GetVulkanImageInfo();

                        auto wds = DescriptorSetBuilder::GetSamplerCubeWDS( this, frame, setIndex, binding, 1,
                                                                            &bufferInfo.ImageInfo );

                        m_PendingDescriptorWrites.push_back( wds );
                    }

                    else
                    {
                        DESERT_VERIFY( false );
                    }
                }

                // =========================================================
                // STORAGE IMAGES
                // =========================================================
                /* for ( const auto& [binding, imageLayout] : descriptorSet.StorageImage2DSamplers )
                 {
                     auto fallbackImage = ShaderResources::API::Vulkan::VulkanUniformImage2D::GetFallbackStorage();

                     auto& imageInfo = fallbackImage->GetDescriptorImageInfo();

                     auto wds =
                          DescriptorSetBuilder::GetStorageWDS( this, frame, setIndex, binding, 1, &imageInfo );

                     m_PendingDescriptorWrites.push_back( wds );
                 }*/
            }
        }

        FlushUpdates();
    }

    void VulkanMaterialBackend::InitializeDefaults()
    {
        InitializeWithFallbacks();
    }

} // namespace Desert::Graphic::API::Vulkan