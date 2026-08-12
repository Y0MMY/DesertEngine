#include "ComputeImages.hpp"
#include "Shader.hpp"
#include "Pipeline.hpp"
#include "Renderer.hpp"

#include <Engine/Runtime/ResourceRegistry.hpp>
#include <Engine/Graphic/Image.hpp>
#include <Engine/Graphic/SkyPayload.hpp>
#include <Engine/ShaderResources/StorageBuffer.hpp>

#include <algorithm>

namespace Desert::Graphic
{
    static constexpr uint32_t kWorkGroupSize = 32;

    namespace
    {
        std::shared_ptr<Shader> GetComputeShader( const std::string& name )
        {
            return Runtime::ResourceRegistry::GetShaderService()->GetByName( name );
        }

        // Resolves an image handle (panorama 2D or source cube) to its base Image*.
        Image* ResolveInput( const Runtime::ImageHandle& handle )
        {
            return Runtime::ResourceRegistry::GetImageService()->Resolve( handle );
        }
    } // namespace

    std::shared_ptr<Image2D> ComputeImages::ProccessForImage2D( const std::shared_ptr<Image>& image )
    {
        return nullptr;
    }

    std::shared_ptr<Image2D> ComputeImages::BakeProceduralPanorama( uint32_t width, uint32_t height,
                                                                    ShaderResources::StorageBuffer* skyParams )
    {
        const auto shader = GetComputeShader( "BakeProceduralSky" );
        if ( !shader || !skyParams )
            return nullptr;

        Core::Formats::Image2DSpecification outputInfo = {
             .Tag        = "ProceduralSkyPanorama",
             .Width      = width,
             .Height     = height,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = 1u,
             .Usage      = Core::Formats::Image2DUsage::Image2D,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        auto output = Image2D::Create( outputInfo, nullptr );
        if ( !output )
            return nullptr;

        auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = "BakeProceduralSky" } );
        pipeline->Invalidate();

        pipeline->SetOutput( 0, output.get(), 0 );
        // The bake reads the sky through the same std430 block the screen pass does — no second hand-packed
        // mirror to keep in step. The binding is passed EXPLICITLY here while the graphics path uses the
        // buffer's own binding number, so the two must be the same constant or the descriptor aliases
        // something else entirely (that failure is a validation error, not a wrong picture).
        pipeline->SetStorageBuffer( kSkyPayloadBinding, skyParams );
        pipeline->Dispatch( std::max( 1u, width / kWorkGroupSize ), std::max( 1u, height / kWorkGroupSize ),
                            1u );

        return output;
    }

    std::shared_ptr<ImageCube> ComputeImages::ProccessForImageCube( const ComputeImagesSpecification& spec )
    {
        const auto shader = GetComputeShader( spec.ShaderName );
        if ( !shader )
            return nullptr;

        Core::Formats::ImageCubeSpecification outputInfo = {
             .Tag        = spec.Tag,
             .Width      = spec.Width,
             .Height     = spec.Height,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = spec.MipLevels,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        auto output = SP_CAST( ImageCube, ImageCube::Create( outputInfo, nullptr ) );

        Image* input = ResolveInput( spec.InputHandle );
        if ( !input || !output )
            return output;

        auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = spec.Tag } );
        pipeline->Invalidate();

        pipeline->SetInput( 0, input );
        pipeline->SetOutput( 1, output.get(), 0 );

        // Dispatched over the FACE, not over the cross. ImageCubeSpecification::Width is the width of the
        // CROSS — VulkanImageCube derives `faceSize = Width / 4` and creates the image at that extent —
        // while the shaders normalize by `imageSize(outputTexture)`, which is the face. Dispatching the
        // cross dimensions therefore launched 4x3 = TWELVE times the invocations the image has texels,
        // and the surplus ones computed a direction outside the face and threw the result away on an
        // out-of-range imageStore. For DiffuseIrradiance, where every invocation integrates 65536
        // samples, that was 4.8 billion samples per bake instead of 400 million — and the bake runs
        // every time the sun rotates 5 degrees. ProccessForImageCubeMips below already had this right.
        const uint32_t faceSize = std::max( 1u, spec.Width / 4u );
        const uint32_t groups   = ( faceSize + kWorkGroupSize - 1u ) / kWorkGroupSize;
        pipeline->Dispatch( groups, groups, 6u );

        return output;
    }

    std::shared_ptr<ImageCube> ComputeImages::ProccessForImageCubeMips( const ComputeImagesSpecification& spec )
    {
        const auto shader = GetComputeShader( spec.ShaderName );
        if ( !shader )
            return nullptr;

        // The cube image's actual FACE size is Width/4 (VulkanImageCube stores a 4x3 cross as 6 faces of
        // Width/4). The mip chain length is bounded by the FACE, not the cross width — requesting more
        // (kMipsCount=11 on a 256 face) is an invalid vkCreateImage (VUID-...-00958) and asks the loop
        // below for mip views that don't exist -> GPU fault / VK_ERROR_DEVICE_LOST. Clamp to the face chain.
        // (PBR reads the count via textureQueryLevels, so fewer mips is safe — the roughness ramp adapts.)
        const uint32_t faceSize = std::max( 1u, spec.Width / 4u );
        uint32_t       maxMips  = 1u;
        for ( uint32_t d = faceSize; d > 1u; d >>= 1 )
            ++maxMips;
        const uint32_t mips = std::clamp( spec.MipLevels, 1u, maxMips );

        Core::Formats::ImageCubeSpecification outputInfo = {
             .Tag        = spec.Tag,
             .Width      = spec.Width,
             .Height     = spec.Height,
             .Format     = Core::Formats::ImageFormat::RGBA32F,
             .Mips       = mips,
             .Properties = Core::Formats::Storage | Core::Formats::Sample,
        };
        auto output = SP_CAST( ImageCube, ImageCube::Create( outputInfo, nullptr ) );

        Image* radiance = ResolveInput( spec.InputHandle );
        if ( !radiance || !output )
            return output;

        auto pipeline = ComputePipeline::Create( { .Shader = shader, .DebugName = spec.Tag } );
        pipeline->Invalidate();

        // One dispatch per mip, each convolved with the matching GGX roughness.
        for ( uint32_t mip = 0; mip < mips; ++mip )
        {
            const float    roughness = ( mips > 1 ) ? static_cast<float>( mip ) / static_cast<float>( mips - 1 )
                                                     : 0.0f;
            // Dispatch over the FACE size at this mip (the shader writes per-face and clamps to imageSize).
            // Previously used the cross width (4x the face) -> 16x wasted threads per dispatch.
            const uint32_t mipSize   = std::max( 1u, faceSize >> mip );
            const uint32_t groups    = ( mipSize + kWorkGroupSize - 1 ) / kWorkGroupSize;

            pipeline->SetInput( 0, radiance );
            pipeline->SetOutput( 1, output.get(), mip );
            pipeline->SetPushConstants( &roughness, sizeof( float ) );
            pipeline->Dispatch( groups, groups, 6u );
        }

        return output;
    }

} // namespace Desert::Graphic
