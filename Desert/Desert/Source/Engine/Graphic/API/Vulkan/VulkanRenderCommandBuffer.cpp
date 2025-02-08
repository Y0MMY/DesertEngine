#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>

namespace Desert::Graphic::API::Vulkan
{
    VulkanRenderCommandBuffer::VulkanRenderCommandBuffer( const std::string& debugName )
    {
    }

    void VulkanRenderCommandBuffer::Init( VulkanQueue* queue )
    {
        m_DrawCommandBuffers    = queue->GetDrawCommandBuffers();
        m_ComputeCommandBuffers = queue->GetComputeCommandBuffers();
    }

} // namespace Desert::Graphic::API::Vulkan
