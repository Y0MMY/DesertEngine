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

        VkCommandBuffer GetCommandBuffer( bool computeBuffer = false, bool secondCommandBuffer = false );

        void Init( VulkanQueue* queue );

        void RegisterUserCommand( std::function<void()> command );
        void ExecuteUserCommands();

    private:
        std::vector<VkCommandBuffer> m_DrawCommandBuffers; // main command buffer | second command buffer
        std::vector<VkCommandBuffer> m_ComputeCommandBuffers;

        std::vector<std::function<void()>> m_UserCommands;
    };
} // namespace Desert::Graphic::API::Vulkan