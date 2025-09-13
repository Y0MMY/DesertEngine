#pragma once

#include <Engine/Assets/AssetManager.hpp>

#include "Services/Mesh/MeshService.hpp"
#include "Services/Skybox/SkyboxService.hpp"
#include "Services/Texture/TextureService.hpp"
#include "Services/Shader/ShaderService.hpp"

namespace Desert::Runtime
{
    // It is worth coming up with an approach where resources will be automatically registered
    class ResourceRegistry final
    {
    public:
        static MeshService*    GetMeshService();
        static SkyboxService*  GetSkyboxService();
        static TextureService* GetTextureService();
        static ShaderService*  GetShaderService();
    };
} // namespace Desert::Runtime