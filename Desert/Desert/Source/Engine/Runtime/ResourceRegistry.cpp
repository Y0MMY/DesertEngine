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

    AnimatedImageService* ResourceRegistry::GetAnimatedImageService()
    {
        static AnimatedImageService animatedImageService;
        return &animatedImageService;
    }

} // namespace Desert::Runtime
