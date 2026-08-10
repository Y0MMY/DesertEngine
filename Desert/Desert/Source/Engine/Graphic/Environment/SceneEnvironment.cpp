#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/ComputeImages.hpp>
#include <Engine/Graphic/SkyRules.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

#include <Common/Core/Constants.hpp>

namespace Desert::Graphic
{
    Environment EnvironmentManager::Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset )
    {
        if ( Common::Utils::FileSystem::GetFileExtension( skyboxAsset->GetMetadata().Filepath ) ==
             ".hdr" ) // TODO: move the logic to the SkyboxAsset and raw data
        {
            // The asset's metadata carries the FULL path (registration owns path composition) — the
            // engine draw layer never glues directory prefixes onto asset paths.
            std::shared_ptr<Texture2D> imagePanorama =
                 Texture2D::Create( { true }, skyboxAsset->GetMetadata().Filepath ).ExtractValue();

            auto* imageService = Runtime::ResourceRegistry::GetImageService();

            // 1) Radiance cube (sharp environment) — also the source the prefilter convolves.
            auto        radianceCube   = ConvertPanoramaToCubemapCross( imagePanorama->GetImageHandle() );
            const auto  radianceHandle = imageService->Register( std::move( radianceCube ),
                                                                 Runtime::ImageHandle::Type::ImageCube );

            // 2) Diffuse irradiance (from the panorama directly).
            auto       diffuseIrradiance       = CreateDiffuseIrradiance( imagePanorama->GetImageHandle() );
            const auto diffuseIrradianceHandle = imageService->Register(
                 std::move( diffuseIrradiance ), Runtime::ImageHandle::Type::ImageCube );

            // 3) Prefiltered specular (real GGX per-mip convolution of the radiance cube).
            auto       prefiltered       = CreatePrefilteredMap( radianceHandle );
            const auto prefilteredHandle = imageService->Register(
                 std::move( prefiltered ), Runtime::ImageHandle::Type::ImageCube );

            return { skyboxAsset->GetMetadata().Filepath, radianceHandle, diffuseIrradianceHandle,
                     prefilteredHandle };
        }

        return {};
    }

    // The cube chain's sizes live in SkyRules.hpp, because the environment-cost report has to compute the
    // same numbers and a second copy of them is how a report starts lying.
    static constexpr uint32_t kEnvFaceMapSize    = kSkyEnvCubeFaceSize;
    static constexpr uint32_t kIrradianceMapSize = kSkyEnvIrradianceFaceSize;
    static constexpr uint32_t kBRDF_LUT_Size     = 256;
    static constexpr uint32_t kMipsCount         = kSkyEnvPrefilterMips;
    static constexpr uint32_t kWorkGroups        = 32;

    std::shared_ptr<Desert::Graphic::ImageCube>
    EnvironmentManager::ConvertPanoramaToCubemapCross( const Runtime::ImageHandle& panorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputHandle = panorama;
        processingInfo.ShaderName  = "PanoramaToCubemap";
        processingInfo.Tag   = "TODO"; // texturePanorama->GetImage()->GetImageSpecification().Tag + "_" + "Cross";
        processingInfo.Width = kEnvFaceMapSize * 4;
        processingInfo.Height    = kEnvFaceMapSize * 3;
        processingInfo.MipLevels = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    EnvironmentManager::CreateDiffuseIrradiance( const Runtime::ImageHandle& panorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputHandle = panorama;
        processingInfo.ShaderName  = "DiffuseIrradiance";
        processingInfo.Tag =
             "TODO"; // texturePanorama->GetImage()->GetImageSpecification().Tag + "_" + "Irradiance";
        processingInfo.Width     = kIrradianceMapSize * 4;
        processingInfo.Height    = kIrradianceMapSize * 3;
        processingInfo.MipLevels = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    Environment EnvironmentManager::CreateProcedural( uint32_t panoramaWidth, uint32_t panoramaHeight,
                                                      ShaderResources::StorageBuffer* skyParams )
    {
        auto* imageService = Runtime::ResourceRegistry::GetImageService();

        // Bake the atmosphere into an equirect HDR panorama, then run the standard IBL pipeline on it.
        auto panorama = ComputeImages::BakeProceduralPanorama( panoramaWidth, panoramaHeight, skyParams );
        if ( !panorama )
            return {};
        const auto panoramaHandle =
             imageService->Register( std::move( panorama ), Runtime::ImageHandle::Type::Image2D );

        // 1) Radiance cube (sharp environment) — also the source the prefilter convolves.
        auto       radianceCube   = ConvertPanoramaToCubemapCross( panoramaHandle );
        const auto radianceHandle = imageService->Register( std::move( radianceCube ),
                                                            Runtime::ImageHandle::Type::ImageCube );

        // 2) Diffuse irradiance (from the panorama directly).
        auto       diffuseIrradiance       = CreateDiffuseIrradiance( panoramaHandle );
        const auto diffuseIrradianceHandle = imageService->Register(
             std::move( diffuseIrradiance ), Runtime::ImageHandle::Type::ImageCube );

        // 3) Prefiltered specular (real GGX per-mip convolution of the radiance cube).
        auto       prefiltered       = CreatePrefilteredMap( radianceHandle );
        const auto prefilteredHandle = imageService->Register( std::move( prefiltered ),
                                                              Runtime::ImageHandle::Type::ImageCube );

        // The panorama was only an intermediate (consumed by the synchronous compute dispatches above).
        imageService->Unregister( panoramaHandle );

        return { Common::Filepath( "ProceduralSky" ), radianceHandle, diffuseIrradianceHandle,
                 prefilteredHandle };
    }

    std::shared_ptr<ImageCube>
    EnvironmentManager::CreatePrefilteredMap( const Runtime::ImageHandle& radianceCube )
    {
        // GGX per-mip convolution of the already-built radiance cube (roughness ramps with mip).
        ComputeImagesSpecification processingInfo;
        processingInfo.InputHandle = radianceCube;
        processingInfo.ShaderName  = "PrefilterEnvMap";
        processingInfo.Tag         = "EnvPrefiltered";
        processingInfo.Width       = kEnvFaceMapSize;
        processingInfo.Height      = kEnvFaceMapSize;
        processingInfo.MipLevels   = kMipsCount;

        return ComputeImages::ProccessForImageCubeMips( processingInfo );
    }

} // namespace Desert::Graphic