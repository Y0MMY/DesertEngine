#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanRenderCommandBuffer::VulkanRenderCommandBuffer( const std::string& debugName )
    {
    }

    void VulkanRenderCommandBuffer::Init( VulkanQueue* queue )
    {

        m_CommandBuffers = queue->GetDrawCommandBuffers();
    }

} // namespace Desert::Graphic::API::Vulkan
