#pragma once

#include <vulkan/vulkan.h>
#include <Engine/Graphic/API/Vulkan/VulkanDevice.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanQueue.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanRenderCommandBuffer : public Common::Singleton<VulkanRenderCommandBuffer>
    {
    public:
        VulkanRenderCommandBuffer( const std::string& debugName );

        inline VkCommandBuffer GetCommandBuffer( uint32_t index, bool computeBuffer = false,
                                                 bool secondCommandBuffer = false )
        {
            return ( computeBuffer ? m_ComputeCommandBuffers[index]
                                   : ( secondCommandBuffer ? m_DrawCommandBuffers[index].second
                                                           : m_DrawCommandBuffers[index].first ) );
        }

        void Init( VulkanQueue* queue );

    private:
        std::vector<std::pair<VkCommandBuffer, VkCommandBuffer>>
                                     m_DrawCommandBuffers; // main command buffer | second command buffer
        std::vector<VkCommandBuffer> m_ComputeCommandBuffers;
    };
} // namespace Desert::Graphic::API::Vulkan