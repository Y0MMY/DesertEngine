#include <Engine/Uniforms/StorageBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Uniforms/API/Vulkan/VulkanStorageBuffer.hpp>

#include <numeric>

namespace Desert::Uniforms
{

    std::shared_ptr<StorageBuffer> StorageBuffer::Create( const std::string_view debugName, uint32_t size,
                                                          uint32_t binding )
    {
        switch ( Graphic::RendererAPI::GetAPIType() )
        {
            case Graphic::RendererAPIType::None:
                return nullptr;
            case Graphic::RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanStorageBuffer>( debugName, size, binding );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::Uniforms
