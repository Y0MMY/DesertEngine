#include <Engine/Graphic/Pipeline.hpp>
#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>

namespace Desert::Graphic
{
    std::shared_ptr<GraphicsPipeline> GraphicsPipeline::Create( const GraphicsPipelineSpecification& spec )
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

    std::shared_ptr<ComputePipeline> ComputePipeline::Create( const ComputePipelineSpecification& spec )
    {
        DESERT_VERIFY( spec.Shader != nullptr, "Empty shader" );
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanPipelineCompute>( spec );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

} // namespace Desert::Graphic
