#include <Engine/Uniforms/UniformImage2D.hpp>
#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanShader.hpp>

#include <Engine/Uniforms/API/Vulkan/VulkanUniformImage2D.hpp>

#include <numeric>

namespace Desert::Uniforms
{

    std::shared_ptr<UniformImage2D> UniformImage2D::Create( const std::string_view debugName, uint32_t binding )
    {
        switch ( Graphic::RendererAPI::GetAPIType() )
        {
            case Graphic::RendererAPIType::None:
                return nullptr;
            case Graphic::RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanUniformImage2D>( debugName, binding );
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

} // namespace Desert::Uniforms
