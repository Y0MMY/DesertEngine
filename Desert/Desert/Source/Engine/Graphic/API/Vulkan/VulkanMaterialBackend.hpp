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

        virtual void ApplyProperties( MaterialExecutor* material ) override;
        virtual void ApplyPushConstants( MaterialExecutor* material, Pipeline* pipeline ) override;
    };
} // namespace Desert::Graphic::API::Vulkan