#include <Engine/Graphic/IndexBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanIndexBuffer.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<IndexBuffer> IndexBuffer::Create( const void* data, uint32_t size, BufferUsage usage )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<VulkanIndexBuffer>::Create( data, size, usage );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create( uint32_t size, BufferUsage usage )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<VulkanIndexBuffer>::Create( size, usage );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }
} // namespace Desert::Graphic