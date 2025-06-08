#include <Engine/Graphic/MipMapGenerator.hpp>
#include <Engine/Graphic/API/Vulkan/VulkanMipMapGenerator.hpp>

#include <Engine/Graphic/RendererAPI.hpp>

namespace Desert::Graphic
{

    std::unique_ptr<MipMap2DGenerator> MipMap2DGenerator::Create( MipGenStrategy strategy )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                if ( strategy == MipGenStrategy::ComputeShader )
                {
                    return std::make_unique<API::Vulkan::VulkanMipMap2DGeneratorCS>();
                }

                DESERT_VERIFY( false, "TransferOps was not impl" );
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

    std::unique_ptr<MipMapCubeGenerator> MipMapCubeGenerator::Create( MipGenStrategy strategy )
    {
        switch ( RendererAPI::GetAPIType() )
        {
            case RendererAPIType::None:
                return nullptr;
            case RendererAPIType::Vulkan:
            {
                if ( strategy == MipGenStrategy::ComputeShader )
                {
                    return std::make_unique<API::Vulkan::VulkanMipMapCubeGeneratorCS>();
                }

                DESERT_VERIFY( false, "TransferOps was not impl" );
            }
        }
        DESERT_VERIFY( false, "Unknown RendererAPI" );
        return nullptr;
    }

} // namespace Desert::Graphic