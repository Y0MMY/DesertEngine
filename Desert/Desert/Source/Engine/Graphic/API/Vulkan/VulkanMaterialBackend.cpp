#include <Engine/Graphic/API/Vulkan/VulkanMaterialBackend.hpp>
#include <Engine/Graphic/API/Vulkan/CommandBufferAllocator.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanRenderer.hpp>

#include <Engine/Uniforms/API/Vulkan/VulkanUniformBuffer.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImage2D.hpp>
#include <Engine/Uniforms/API/Vulkan/VulkanUniformImageCube.hpp>

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
            vkDestroyDescriptorPool( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(), m_DescriptorPool,
                                     nullptr );
        }
    }

    void VulkanMaterialBackend::CreateDescriptorPool()
    {
        const uint32_t framesInFlight = 3u; // EngineContext::Get()->GetFramesInFlight();
        const uint32_t setCount       = m_VulkanShader->GetDescriptorSetLayoutCount();

        std::vector<VkDescriptorPoolSize> poolSizes = {
             { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * framesInFlight * setCount },
             { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * framesInFlight * setCount },
             { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 * framesInFlight * setCount },
             { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 * framesInFlight * setCount } };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>( poolSizes.size() );
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = framesInFlight * setCount * 2; // Немного запаса
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RESULT( vkCreateDescriptorPool( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                                 &poolInfo, nullptr, &m_DescriptorPool ) );
    }

    void VulkanMaterialBackend::AllocateDescriptorSets()
    {
        const uint32_t framesInFlight = 3u; // EngineContext::Get()->GetFramesInFlight();
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

            VK_CHECK_RESULT( vkAllocateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
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
            if ( auto vulkanBuffer = sp_cast<Uniforms::API::Vulkan::VulkanUniformBuffer>( bufferInfo ) )
            {
                auto& bufferInfo = vulkanBuffer->GetDescriptorBufferInfo();
                auto  wds        = DescriptorSetBuilder::GetUniformWDS( this, frameIndex, 0, // set 0
                                                                        vulkanBuffer->GetBinding(), 1U, &bufferInfo );

                m_PendingDescriptorWrites.push_back( wds );
                uniformProp->MarkClean();
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
            if ( auto vulkanImage = sp_cast<Uniforms::API::Vulkan::VulkanUniformImage2D>( imageUniform ) )
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
            if ( auto vulkanImage = sp_cast<Uniforms::API::Vulkan::VulkanUniformImageCube>( imageUniform ) )
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
            vkUpdateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                    static_cast<uint32_t>( m_PendingDescriptorWrites.size() ),
                                    m_PendingDescriptorWrites.data(), 0, nullptr );
            m_PendingDescriptorWrites.clear();
        }

        m_PendingDescriptorWrites.clear();
    }

} // namespace Desert::Graphic::API::Vulkan