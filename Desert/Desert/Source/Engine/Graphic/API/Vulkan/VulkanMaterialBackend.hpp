#pragma once

#include <Engine/Graphic/Materials/MaterialBackend.hpp>

namespace Desert::Graphic::API::Vulkan
{
    class VulkanMaterialBackend final : public MaterialBackend
    {
    public:
        virtual void ApplyUniformBuffer( MaterialProperty* prop ) override;
        virtual void ApplyTexture2D( MaterialProperty* prop ) override;
        virtual void ApplyTextureCube( MaterialProperty* prop ) override;

        virtual void ApplyProperties( Material* material ) override;
        virtual void ApplyPushConstants( Material* material, Pipeline* pipeline ) override;
    };
} // namespace Desert::Graphic::API::Vulkan