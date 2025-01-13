#include <Engine/Graphic/UniformBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanUniformBuffer.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<UniformBuffer> UniformBuffer::Create( uint32_t size, uint32_t binding )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanUniformBuffer>( size, binding );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::Graphic
