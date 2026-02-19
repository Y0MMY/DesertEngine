#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/ComputeImages.hpp>

#include <Engine/Runtime/ResourceRegistry.hpp>

namespace Desert::Graphic
{
    Environment EnvironmentManager::Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset )
    {
        if ( Common::Utils::FileSystem::GetFileExtension( skyboxAsset->GetMetadata().Filepath ) ==
             ".hdr" ) // TODO: move the logic to the SkyboxAsset and raw data
        {
            std::shared_ptr<Texture2D> imagePanorama =
                 Texture2D::Create( { true }, skyboxAsset->GetMetadata().Filepath ).ExtractValue();

            auto crossCubemap      = ConvertPanoramaToCubemapCross( imagePanorama );
            auto diffuseIrradiance = CreateDiffuseIrradiance( imagePanorama );
            auto prefiltered       = CreatePrefilteredMap( imagePanorama );

            const auto& crossCubemapHandle = Runtime::ResourceRegistry::GetImageService()->Register(
                 std::move( crossCubemap ), Runtime::ImageHandle::Type::ImageCube );

            const auto& diffuseIrradianceHandle = Runtime::ResourceRegistry::GetImageService()->Register(
                 std::move( diffuseIrradiance ), Runtime::ImageHandle::Type::ImageCube );

            const auto& prefilteredHandle = Runtime::ResourceRegistry::GetImageService()->Register(
                 std::move( prefiltered ), Runtime::ImageHandle::Type::ImageCube );

            return { skyboxAsset->GetMetadata().Filepath, crossCubemapHandle, diffuseIrradianceHandle,
                     prefilteredHandle };
        }

        return {};
    }

    static constexpr uint32_t kEnvFaceMapSize    = 1024;
    static constexpr uint32_t kIrradianceMapSize = 32;
    static constexpr uint32_t kBRDF_LUT_Size     = 256;
    static constexpr uint32_t kMipsCount         = 11;
    static constexpr uint32_t kWorkGroups        = 32;

    std::shared_ptr<Desert::Graphic::ImageCube>
    EnvironmentManager::ConvertPanoramaToCubemapCross( const std::shared_ptr<Texture2D>& texturePanorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputHandle = texturePanorama->GetImageHandle();
        processingInfo.ShaderName  = "PanoramaToCubemap";
        processingInfo.Tag   = "TODO"; // texturePanorama->GetImage()->GetImageSpecification().Tag + "_" + "Cross";
        processingInfo.Width = kEnvFaceMapSize * 4;
        processingInfo.Height    = kEnvFaceMapSize * 3;
        processingInfo.MipLevels = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    EnvironmentManager::CreateDiffuseIrradiance( const std::shared_ptr<Texture2D>& texturePanorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputHandle = texturePanorama->GetImageHandle();
        processingInfo.ShaderName  = "DiffuseIrradiance";
        processingInfo.Tag =
             "TODO"; // texturePanorama->GetImage()->GetImageSpecification().Tag + "_" + "Irradiance";
        processingInfo.Width     = kIrradianceMapSize * 4;
        processingInfo.Height    = kIrradianceMapSize * 3;
        processingInfo.MipLevels = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    std::shared_ptr<ImageCube>
    EnvironmentManager::CreatePrefilteredMap( const std::shared_ptr<Texture2D>& texturePanorama )
    {
        ComputeImagesSpecification processingInfo;

        processingInfo.ShaderName = "PanoramaToCubemap";
        processingInfo.Tag =
             "TODO"; // texturePanorama->GetImage()->GetImageSpecification().Tag + "_" + "Prefiltered";
        processingInfo.Width     = kEnvFaceMapSize * 4;
        processingInfo.Height    = kEnvFaceMapSize * 3;
        processingInfo.MipLevels = kMipsCount;

        const auto  cubeCross       = ComputeImages::ProccessForImageCube( processingInfo );
        const auto& cubeCrossHandle = Runtime::ResourceRegistry::GetImageService()->Register(
             std::move( cubeCross ), Runtime::ImageHandle::Type::ImageCube );
        const auto generatorMips = MipMapCubeGenerator::Create( MipGenStrategy::TransferOps );
        generatorMips->GenerateMips( cubeCross );

        {
            ComputeImagesSpecification processingInfo;
            processingInfo.InputHandle = cubeCrossHandle;
            processingInfo.ShaderName  = "PrefilterEnvMap";
            processingInfo.Tag         = cubeCross->GetImageSpecification().Tag + "_" + "Prefiltered";
            processingInfo.Width       = kEnvFaceMapSize;
            processingInfo.Height      = kEnvFaceMapSize;
            processingInfo.MipLevels   = kMipsCount;
            // Process mips
            ComputeImages::ProccessForImageCubeMips( processingInfo );
        }

        return cubeCross;
    }

} // namespace Desert::Graphic