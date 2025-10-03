#include <Engine/Uniforms/UniformBuffer.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Uniforms/API/Vulkan/VulkanUniformBuffer.hpp>

#include <numeric>

namespace Desert::Uniforms
{

    UniformBuffer::UniformBuffer( const Core::Models::UniformBuffer& uniform ) : m_UniformModel( uniform )
    {
    }

    std::shared_ptr<UniformBuffer> UniformBuffer::Create( const Core::Models::UniformBuffer& uniform )
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

} // namespace Desert::Uniforms
