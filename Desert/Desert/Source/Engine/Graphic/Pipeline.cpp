#include <Engine/Graphic/Pipeline.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>

namespace Desert::Graphic
{

    std::shared_ptr<Pipeline> Pipeline::Create( const PipelineSpecification& spec )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanPipeline>( spec );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::Graphic