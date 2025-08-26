#include <Engine/Graphic/Environment/SceneEnvironment.hpp>

#include <Engine/Graphic/RendererAPI.hpp>
#include <Engine/Graphic/Renderer.hpp>
#include <Engine/Graphic/ComputeImages.hpp>

namespace Desert::Graphic
{
    Environment EnvironmentManager::Create( const std::shared_ptr<Assets::SkyboxAsset>& skyboxAsset )
    {
        if ( Common::Utils::FileSystem::GetFileExtension( skyboxAsset->GetMetadata().Filepath ) ==
             ".hdr" ) // TODO: move the logic to the SkyboxAsset and raw data
        {
            std::shared_ptr<Texture2D> imagePanorama =
                 Texture2D::Create( { true }, skyboxAsset->GetMetadata().Filepath );
            imagePanorama->Invalidate();

          //  const auto crossCubemap = ConvertPanoramaToCubemapCross( imagePanorama );
            // const auto diffuseIrradiance = CreateDiffuseIrradiance( imagePanorama );
           const auto prefiltered = CreatePrefilteredMap( imagePanorama );

            return { skyboxAsset->GetMetadata().Filepath, prefiltered, prefiltered, prefiltered };
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
        processingInfo.InputImage = texturePanorama->GetImage2D();
        processingInfo.ShaderName = "PanoramaToCubemap.glsl";
        processingInfo.Tag        = texturePanorama->GetImage2D()->GetImageSpecification().Tag + "_" + "Cross";
        processingInfo.Width      = kEnvFaceMapSize * 4;
        processingInfo.Height     = kEnvFaceMapSize * 3;
        processingInfo.MipLevels  = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    std::shared_ptr<Desert::Graphic::ImageCube>
    EnvironmentManager::CreateDiffuseIrradiance( const std::shared_ptr<Texture2D>& texturePanorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputImage = texturePanorama->GetImage2D();
        processingInfo.ShaderName = "DiffuseIrradiance.glsl";
        processingInfo.Tag       = texturePanorama->GetImage2D()->GetImageSpecification().Tag + "_" + "Irradiance";
        processingInfo.Width     = kIrradianceMapSize * 4;
        processingInfo.Height    = kIrradianceMapSize * 3;
        processingInfo.MipLevels = 1u;

        return ComputeImages::ProccessForImageCube( processingInfo );
    }

    std::shared_ptr<ImageCube>
    EnvironmentManager::CreatePrefilteredMap( const std::shared_ptr<Texture2D>& texturePanorama )
    {
        ComputeImagesSpecification processingInfo;
        processingInfo.InputImage = texturePanorama->GetImage2D();
        processingInfo.ShaderName = "PanoramaToCubemap.glsl";
        processingInfo.Tag        = texturePanorama->GetImage2D()->GetImageSpecification().Tag + "_" + "Prefiltered";
        processingInfo.Width      = kEnvFaceMapSize * 4;
        processingInfo.Height     = kEnvFaceMapSize * 3;
        processingInfo.MipLevels  = kMipsCount;

        const auto cubeCross     = ComputeImages::ProccessForImageCube( processingInfo );
        const auto generatorMips = MipMapCubeGenerator::Create( MipGenStrategy::TransferOps );
        generatorMips->GenerateMips( cubeCross );

        {
            ComputeImagesSpecification processingInfo;
            processingInfo.InputImage = cubeCross;
            processingInfo.ShaderName = "PrefilterEnvMap.glsl";
            processingInfo.Tag        = cubeCross->GetImageSpecification().Tag + "_" + "Prefiltered";
            processingInfo.Width      = kEnvFaceMapSize;
            processingInfo.Height     = kEnvFaceMapSize;
            processingInfo.MipLevels  = kMipsCount;
            // Process mips
            ComputeImages::ProccessForImageCubeMips( processingInfo );
        }

        return cubeCross;
    }

} // namespace Desert::Graphic