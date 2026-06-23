#include "ComputeImages.hpp"
#include "Shader.hpp"
#include "Pipeline.hpp"
#include "Renderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Image.hpp>

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

        auto outputCube = SP_CAST( ImageCube, outputImage );

        auto* inputImage = static_cast<Image2D*>(
            Runtime::ResourceRegistry::GetImageService()->Resolve( spec.InputHandle ) );

        if ( inputImage && outputCube )
        {
            Renderer::GetInstance().ImmediateComputeDispatch(
                computePipeline.get(), inputImage, outputCube.get(),
                workGroupsX, workGroupsY, workGroupsZ );
        }

        return outputCube;
    }

    void ComputeImages::ProccessForImageCubeMips( const ComputeImagesSpecification& spec )
    {
        // ... similar update ...
    }

} // namespace Desert::Graphic
