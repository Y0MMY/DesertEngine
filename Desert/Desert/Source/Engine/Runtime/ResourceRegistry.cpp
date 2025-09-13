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

} // namespace Desert::Runtime