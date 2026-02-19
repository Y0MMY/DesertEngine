#pragma once

#include <Engine/Assets/AssetManager.hpp>

#include "Services/Mesh/MeshService.hpp"
#include "Services/Skybox/SkyboxService.hpp"
#include "Services/Texture/TextureService.hpp"
#include "Services/Shader/ShaderService.hpp"
#include "Services/Image/ImageService.hpp"

namespace Desert::Runtime
{
    // It is worth coming up with an approach where resources will be automatically registered

    // NOTE:
    // ResourceRegistry is allowed ONLY in runtime/render layer.
    // ECS systems must not resolve GPU resources directly.
    class ResourceRegistry final
    {
    public:
        static MeshService*      GetMeshService();
        static SkyboxService*    GetSkyboxService();
        static TextureService*   GetTextureService();
        static ShaderService*    GetShaderService();
        static ImageService*     GetImageService();
    };
} // namespace Desert::Runtime