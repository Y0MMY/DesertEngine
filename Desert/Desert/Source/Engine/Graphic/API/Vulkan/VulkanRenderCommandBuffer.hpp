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

        inline VkCommandBuffer RT_GetCommandBuffer( uint32_t index, bool computeBuffer = false )
        {
            return m_CommandBuffers[index];
        }

        void Init( VulkanQueue* queue );

    private:
        std::vector<VkCommandBuffer> m_CommandBuffers;
    };
} // namespace Desert::Graphic::API::Vulkan