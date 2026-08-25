#include "ResourceRegistry.hpp"

namespace Desert::Runtime
{
    MeshService* ResourceRegistry::GetMeshService()
    {
        static MeshService meshService;
        return &meshService;
    }

    SkyboxService* ResourceRegistry::GetSkyboxService()
    {
        static SkyboxService skyboxService;
        return &skyboxService;
    }

    TextureService* ResourceRegistry::GetTextureService()
    {
        static TextureService textureService;
        return &textureService;
    }

    ShaderService* ResourceRegistry::GetShaderService()
    {
        static ShaderService shaderService;
        return &shaderService;
    }

    ImageService* ResourceRegistry::GetImageService()
    {
        static ImageService imageService;
        return &imageService;
    }

    MaterialService* ResourceRegistry::GetMaterialService()
    {
        static MaterialService materialService;
        return &materialService;
    }

    FontService* ResourceRegistry::GetFontService()
    {
        static FontService fontService;
        return &fontService;
    }

    IconService* ResourceRegistry::GetIconService()
    {
        static IconService iconService;
        return &iconService;
    }

    AnimatedImageService* ResourceRegistry::GetAnimatedImageService()
    {
        static AnimatedImageService animatedImageService;
        return &animatedImageService;
    }

    VideoService* ResourceRegistry::GetVideoService()
    {
        static VideoService videoService;
        return &videoService;
    }

    CloudNoiseService* ResourceRegistry::GetCloudNoiseService()
    {
        static CloudNoiseService cloudNoiseService;
        return &cloudNoiseService;
    }

    CloudTypeService* ResourceRegistry::GetCloudTypeService()
    {
        static CloudTypeService cloudTypeService;
        return &cloudTypeService;
    }

    CloudModellingService* ResourceRegistry::GetCloudModellingService()
    {
        static CloudModellingService cloudModellingService;
        return &cloudModellingService;
    }

    CloudLayoutService* ResourceRegistry::GetCloudLayoutService()
    {
        static CloudLayoutService cloudLayoutService;
        return &cloudLayoutService;
    }

} // namespace Desert::Runtime
