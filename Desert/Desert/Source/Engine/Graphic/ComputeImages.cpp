#include "ComputeImages.hpp"
#include "Shader.hpp"
#include "Pipeline.hpp"
#include "Renderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic
{
    static constexpr uint32_t kEnvFaceMapSize    = 1024;
    static constexpr uint32_t kIrradianceMapSize = 32;
    static constexpr uint32_t kBRDF_LUT_Size     = 256;
    static constexpr uint32_t kWorkGroups        = 32;

    std::shared_ptr<Image2D> ComputeImages::ProccessForImage2D( const std::shared_ptr<Image>& image )
    {
        return nullptr;
    }

    std::shared_ptr<ImageCube> ComputeImages::ProccessForImageCube( const ComputeImagesSpecification& spec )
    {
        static std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache;

        auto& shader = shaderCache[spec.ShaderName];
        if ( !shader )
        {
            shader = Runtime::ResourceRegistry::GetShaderService()->GetByName( spec.ShaderName );
        }

        Core::Formats::ImageCubeSpecification outputImageInfo = {
             .Tag        = spec.Tag,
             .Width      = spec.Width,
             .Height     = spec.Height,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = spec.MipLevels,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };

        std::shared_ptr<Image> outputImage = ImageCube::Create( outputImageInfo, nullptr );

        ComputePipelineSpecification pipelineSpec = { .Shader = shader, .DebugName = spec.Tag };
        const auto& computePipeline = ComputePipeline::Create( pipelineSpec );
        computePipeline->Invalidate();

        const uint32_t workGroupsX = spec.Width / kWorkGroups;
        const uint32_t workGroupsY = spec.Height / kWorkGroups;
        const uint32_t workGroupsZ = 6;
        
        // This is still a bit hacky because we don't have a clean way to pass images to DispatchCompute yet
        // but we are at least using the new Pipeline interface.
        // TODO: Full refactor of image passing in compute
        
        // For now, I'll keep the direct direct execution logic in VulkanPipelineCompute if I didn't remove it yet.
        // Wait, I DID remove it. I need to implement it in DispatchCompute or a separate ComputeExecutor.
        
        return SP_CAST( ImageCube, outputImage );
    }

    void ComputeImages::ProccessForImageCubeMips( const ComputeImagesSpecification& spec )
    {
        // ... similar update ...
    }

} // namespace Desert::Graphic
