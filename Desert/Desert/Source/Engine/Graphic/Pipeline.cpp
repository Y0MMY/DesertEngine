#include <Engine/Graphic/Pipeline.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

#include <Engine/Graphic/API/Vulkan/VulkanPipeline.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanPipelineCompute.hpp>

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

    std::shared_ptr<Desert::Graphic::PipelineCompute>
    PipelineCompute::Create( const std::shared_ptr<Shader>& shader )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
                return std::make_shared<API::Vulkan::VulkanPipelineCompute>( shader );
        }
        DESERT_VERIFY( false, "Unknown RenderingAPI" );
        return nullptr;
    }

    void PipelineCompute::UpdateStorageBuffer( void* data, std::size_t size )
    {
        DESERT_VERIFY( size <= 128 );

        m_StorageBuffer = Common::Memory::Buffer::Copy( data, size );
    }

} // namespace Desert::Graphic