#include "ComputeImages.hpp"
#include "Shader.hpp"
#include "Pipeline.hpp"

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
        static std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache; // TODO: remove

        auto& shader = shaderCache[spec.ShaderName];
        if ( !shader )
        {
            shader = Shader::Create( spec.ShaderName );
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

        const auto& computePipeline = PipelineCompute::Create( shader );
        computePipeline->Invalidate();

        const uint32_t workGroupsX = spec.Width / kWorkGroups;
        const uint32_t workGroupsY = spec.Height / kWorkGroups;
        const uint32_t workGroupsZ = 6;

        computePipeline->Execute( spec.InputImage, outputImage, workGroupsX, workGroupsY, workGroupsZ );

        return SP_CAST( ImageCube, outputImage );
    }

    void ComputeImages::ProccessForImageCubeMips( const ComputeImagesSpecification& spec )
    {
        DESERT_VERIFY( spec.MipLevels > 1, "Use ProccessForImageCube for single mip level" );

        static std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache;

        auto& shader = shaderCache[spec.ShaderName];
        if ( !shader )
        {
            shader = Shader::Create( spec.ShaderName );
        }

        const auto& computePipeline = PipelineCompute::Create( shader );
        computePipeline->Invalidate();

        const float deltaRoughness = 1.0f / std::max( float( spec.MipLevels - 1 ), 1.0f );
        // Process each mip level
        for ( uint32_t mipLevel = 1; mipLevel < spec.MipLevels; ++mipLevel )
        {
            const uint32_t mipWidth  = spec.Width >> mipLevel;
            const uint32_t mipHeight = spec.Height >> mipLevel;

            const uint32_t workGroupsX = std::max( 1u, mipWidth / kWorkGroups );
            const uint32_t workGroupsY = std::max( 1u, mipHeight / kWorkGroups );
            const uint32_t workGroupsZ = 6;

            float updatedDelta = deltaRoughness * mipLevel;

            computePipeline->UpdateStorageBuffer( (void*)&updatedDelta, sizeof( float ) );
            computePipeline->ExecuteMipLevel( spec.InputImage, mipLevel, workGroupsX, workGroupsY, workGroupsZ );
        }
    }

} // namespace Desert::Graphic