#include <Engine/Graphic/VertexBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanVertexBuffer.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Desert::Graphic::VertexBuffer>
    VertexBuffer::Create( void* data, uint32_t size, BufferUsage usage /*= BufferUsage::Static */ )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanVertexBuffer>( data, size, usage );
            }
        }

        DESERT_VERIFY( false );
        return nullptr;
    }

    std::shared_ptr<Desert::Graphic::VertexBuffer>
    VertexBuffer::Create( uint32_t size, BufferUsage usage /*= BufferUsage::Dynamic */ )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::Vulkan:
            {
                return std::make_shared<API::Vulkan::VulkanVertexBuffer>( size, usage );
            }
        }

        DESERT_VERIFY( false );
        return nullptr;
    }

} // namespace Desert::Graphic