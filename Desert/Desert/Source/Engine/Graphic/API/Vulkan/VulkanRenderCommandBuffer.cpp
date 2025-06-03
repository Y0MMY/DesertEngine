#include <Engine/Graphic/API/Vulkan/VulkanRenderCommandBuffer.hpp>
#include <Engine/Graphic/Renderer.hpp>

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

    VkCommandBuffer VulkanRenderCommandBuffer::GetCommandBuffer( bool computeBuffer /*= false*/,
                                                                 bool secondCommandBuffer /*= false*/ )
    {
        uint32_t frameIndex = Renderer::GetInstance().GetCurrentFrameIndex();

        return ( computeBuffer ? m_ComputeCommandBuffers[frameIndex]
                               : ( secondCommandBuffer ? m_DrawCommandBuffers[frameIndex]
                                                       : m_DrawCommandBuffers[frameIndex] ) );
    }

    void VulkanRenderCommandBuffer::RegisterUserCommand( std::function<void()> command )
    {
        m_UserCommands.push_back( std::move( command ) );
    }

    void VulkanRenderCommandBuffer::ExecuteUserCommands()
    {
        for ( const auto& func : m_UserCommands )
        {
            func();
        }

        m_UserCommands.clear();
    }

} // namespace Desert::Graphic::API::Vulkan
