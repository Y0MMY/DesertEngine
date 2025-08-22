#include <Engine/Graphic/API/Vulkan/VulkanDescriptorManager.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanUtils/VulkanHelper.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanContext.hpp>

namespace Desert::Graphic::API::Vulkan
{

    VulkanDescriptorManager::VulkanDescriptorManager()
    {
    }

    VulkanDescriptorManager::~VulkanDescriptorManager()
    {
        for ( auto& frame : m_Frames )
        {
            if ( frame.DescriptorPool != VK_NULL_HANDLE )
            {
                vkDestroyDescriptorPool( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                         frame.DescriptorPool, nullptr );
            }
        }
    }

    void VulkanDescriptorManager::Initialize( uint32_t framesInFlight )
    {
        m_FramesInFlight = framesInFlight;
        m_Frames.resize( framesInFlight );

        for ( auto& frame : m_Frames )
        {
            frame.DescriptorPool = CreateDescriptorPool();
        }
    }

    Common::Result<VulkanDescriptorManager::DescriptorSetInfo>
    VulkanDescriptorManager::GetDescriptorSet( const std::shared_ptr<VulkanShader>& shader, uint32_t frameIndex,
                                               uint32_t setIndex )
    {
        if ( frameIndex >= m_Frames.size() )
        {
            return Common::MakeError<DescriptorSetInfo>( "Invalid frame index" );
        }

        auto& frame = m_Frames[frameIndex];

        VkDescriptorSetLayout layout = shader->GetDescriptorSetLayout( setIndex );
        if ( layout == VK_NULL_HANDLE )
        {
            return Common::MakeSuccess( DescriptorSetInfo{ VK_NULL_HANDLE, VK_NULL_HANDLE } );
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = frame.DescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &layout;

        VkDescriptorSet descriptorSet;
        VkResult result = vkAllocateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                                    &allocInfo, &descriptorSet );

        if ( result != VK_SUCCESS )
        {
            return Common::MakeFormattedError<DescriptorSetInfo>( "Failed to allocate descriptor set: {}",
                                                                  VkResultToString( result ) );
        }

        m_Frames[frameIndex].LastDescriptorSets[shader].resize( setIndex + 1 );
        m_Frames[frameIndex].LastDescriptorSets[shader][setIndex] = descriptorSet;
        return Common::MakeSuccess( DescriptorSetInfo{ descriptorSet, layout } );
    }

    void VulkanDescriptorManager::UpdateDescriptorSet( const std::shared_ptr<VulkanShader>& shader,
                                                       uint32_t frameIndex, uint32_t setIndex,
                                                       const std::vector<VkWriteDescriptorSet>& writes )
    {
        vkUpdateDescriptorSets( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                static_cast<uint32_t>( writes.size() ), writes.data(), 0, nullptr );
    }

    Common::Result<VulkanDescriptorManager::DescriptorSetInfo>
    VulkanDescriptorManager::GetLast( const std::shared_ptr<VulkanShader>& shader, uint32_t frameIndex,
                                      uint32_t setIndex ) const
    {
        if ( frameIndex >= m_Frames.size() )
        {
            return Common::MakeError<DescriptorSetInfo>( "Invalid frame index" );
        }

        const auto& frame = m_Frames[frameIndex];
        auto        it    = frame.LastDescriptorSets.find( shader );

        if ( it == frame.LastDescriptorSets.end() || setIndex >= it->second.size() )
        {
            return Common::MakeError<DescriptorSetInfo>( "No descriptor set found" );
        }

        return Common::MakeSuccess(
             DescriptorSetInfo{ it->second[setIndex], shader->GetDescriptorSetLayout( setIndex ) } );
    }

    void VulkanDescriptorManager::CleanupFrame( uint32_t frameIndex )
    {
        if ( frameIndex >= m_Frames.size() )
            return;

        vkResetDescriptorPool( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                               m_Frames[frameIndex].DescriptorPool, 0 );
    }

    void VulkanDescriptorManager::BindDescriptorSets( VkCommandBuffer cmdBuffer, VkPipelineBindPoint bindPoint,
                                                      VkPipelineLayout                     layout,
                                                      const std::shared_ptr<VulkanShader>& shader,
                                                      uint32_t frameIndex, uint32_t firstSet )
    {
        if ( frameIndex >= m_Frames.size() )
        {
            LOG_ERROR( "Invalid frame index: {}", frameIndex );
            return;
        }

        auto& frame = m_Frames[frameIndex];
        auto  it    = frame.LastDescriptorSets.find( shader );

        if ( it == frame.LastDescriptorSets.end() || it->second.empty() )
        {
            LOG_ERROR( "No descriptor sets found for shader" );
            return;
        }

        const auto& descriptorSets = it->second;

        if ( firstSet >= descriptorSets.size() )
        {
            LOG_ERROR( "Invalid firstSet index: {}", firstSet );
            return;
        }

        uint32_t setCount = static_cast<uint32_t>( descriptorSets.size() ) - firstSet;

        vkCmdBindDescriptorSets( cmdBuffer, bindPoint, layout, firstSet, setCount,
                                 descriptorSets.data() + firstSet, 0, nullptr );
    }

    VkDescriptorPool VulkanDescriptorManager::CreateDescriptorPool()
    {
        std::vector<VkDescriptorPoolSize> poolSizes = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                                                        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                                                        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 },
                                                        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 } };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>( poolSizes.size() );
        poolInfo.pPoolSizes    = poolSizes.data();
        poolInfo.maxSets       = 1000;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VkDescriptorPool pool;
        VK_CHECK_RESULT( vkCreateDescriptorPool( VulkanLogicalDevice::GetInstance().GetVulkanLogicalDevice(),
                                                 &poolInfo, nullptr, &pool ) );

        return pool;
    }

} // namespace Desert::Graphic::API::Vulkan