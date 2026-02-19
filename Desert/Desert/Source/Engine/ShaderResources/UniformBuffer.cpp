#include <Engine/ShaderResources/UniformBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/ShaderResources/API/Vulkan/VulkanUniformBuffer.hpp>

#include <numeric>

namespace Desert::ShaderResources
{

    UniformBuffer::UniformBuffer( const ShaderLayout::UniformBuffer& uniform ) : m_UniformModel( uniform )
    {
    }

    std::shared_ptr<UniformBuffer> UniformBuffer::Create( const ShaderLayout::UniformBuffer& uniform )
    {
        switch ( Graphic::RendererAPI::GetAPIType() )
        {
            case Graphic::RendererAPIType::None:
                return nullptr;
            case Graphic::RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanUniformBuffer>( uniform );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::ShaderResources
