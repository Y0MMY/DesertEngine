
#pragma once

#include <Engine/Assets/AssetManager.hpp>

#include "Services/Mesh/MeshService.hpp"
#include "Services/Material/MaterialService.hpp"
#include "Services/Skybox/SkyboxService.hpp"
#include "Services/Texture/TextureService.hpp"
#include "Services/Shader/ShaderService.hpp"
#include "Services/Image/ImageService.hpp"
#include "Services/Font/FontService.hpp"
#include "Services/Icon/IconService.hpp"
#include "Services/AnimatedImage/AnimatedImageService.hpp"
#include "Services/Video/VideoService.hpp"
#include "Services/CloudNoise/CloudNoiseService.hpp"
#include "Services/CloudType/CloudTypeService.hpp"
#include "Services/CloudModelling/CloudModellingService.hpp"

namespace Desert::Runtime
{
    // It is worth coming up with an approach where resources will be automatically registered

    // NOTE:
    // ResourceRegistry is allowed ONLY in runtime/render layer.
    // ECS systems must not resolve GPU resources directly.
    class ResourceRegistry final
    {
    public:
        static MaterialService* GetMaterialService();
        static MeshService*     GetMeshService();
        static SkyboxService*   GetSkyboxService();
        static TextureService*  GetTextureService();
        static ShaderService*   GetShaderService();
        static ImageService*    GetImageService();
        static FontService*     GetFontService();
        static IconService*     GetIconService();

        static AnimatedImageService* GetAnimatedImageService();
        static VideoService*         GetVideoService();
        static CloudNoiseService*    GetCloudNoiseService();
        static CloudTypeService*     GetCloudTypeService();
        // The sculpted hero-cloud bodies (`.dcmv`), slot A of the cloud field's seam.
        static CloudModellingService* GetCloudModellingService();
    };
} // namespace Desert::Runtime
