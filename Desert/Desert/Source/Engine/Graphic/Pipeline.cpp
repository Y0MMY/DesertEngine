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

    Common::ResultStr<std::shared_ptr<ComputePipeline>>
    ComputePipeline::Create( const ComputePipelineSpecification& spec )
    {
        // ASKED BEFORE ANYTHING IS CONSTRUCTED, because the leaf's constructor already dereferences
        // spec.Shader (it builds a VulkanMaterialBackend from it). The `DESERT_VERIFY( spec.Shader )`
        // that stood here was not a refusal at all: it logs and then takes the process down, which is
        // the outcome the caller is being handed a Result in order to avoid.
        if ( const auto buildable = CheckComputePipelineSpecification( spec ); !buildable )
        {
            return Common::MakeError<std::shared_ptr<ComputePipeline>>( buildable.GetError() );
        }

        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                // A REFUSAL WITH A REASON, not a null nobody could tell apart from "the shader was bad".
                return Common::MakeError<std::shared_ptr<ComputePipeline>>(
                     std::string( "ComputePipeline '" ) + spec.DebugName + "': no rendering API is selected." );
            case RendererAPIType::Vulkan:
            {
                auto pipeline = std::make_shared<API::Vulkan::VulkanPipelineCompute>( spec );
                pipeline->Invalidate();

                // THE HANDLE, NOT A FLAG. Invalidate's own refusal (a shader with no compute stage in
                // it) leaves the VkPipeline null, and asking the object what it actually built is the
                // one question that cannot drift away from what it did — a second `bool m_Built` beside
                // it would be exactly the mirror this engine keeps finding out of step with its subject.
                if ( pipeline->GetVkPipeline() == VK_NULL_HANDLE )
                {
                    return Common::MakeError<std::shared_ptr<ComputePipeline>>(
                         std::string( "ComputePipeline '" ) + spec.DebugName +
                         "': the Vulkan pipeline was not built (see the error above)." );
                }
                return Common::MakeSuccess<std::shared_ptr<ComputePipeline>>( std::move( pipeline ) );
            }
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return Common::MakeError<std::shared_ptr<ComputePipeline>>( "Unknown RenderingAPI" );
    }

} // namespace Desert::Graphic
